#include "probe_ota.h"
#include "device_config.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include <string.h>

// One NVS blob keeps signed intent, checkpoint and phase atomic across power loss.
static struct {
    uint32_t format, phase, offset, address;
    uint8_t manifest[TB_MANIFEST_SIGNED_SIZE];
} journal;
static nvs_handle_t storage;
static esp_ota_handle_t handle;
static const esp_partition_t *destination;
static uint32_t received;
static uint8_t scratch[4096];

static bool hash(const uint8_t *data, size_t size, uint8_t digest[32]) {
    size_t actual=0;
    return psa_hash_compute(PSA_ALG_SHA_256,data,size,digest,32,&actual)==PSA_SUCCESS && actual==32;
}
static bool signature_valid(const uint8_t *manifest) {
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes,256); psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t key=0;
    if (psa_import_key(&attributes,TB_UPDATE_PUBLIC_KEY,65,&key)!=PSA_SUCCESS) return false;
    uint8_t digest[32];
    bool valid=hash(manifest,TB_MANIFEST_SIZE,digest) &&
        psa_verify_hash(key,PSA_ALG_ECDSA(PSA_ALG_SHA_256),digest,32,manifest+TB_MANIFEST_SIZE,64)==PSA_SUCCESS;
    psa_destroy_key(key); return valid;
}
static bool save(void) {
    return nvs_set_blob(storage,"job",&journal,sizeof journal)==ESP_OK && nvs_commit(storage)==ESP_OK;
}
static enum tb_status failed(enum tb_status status) {
    if (handle) { esp_ota_abort(handle); handle=0; }
    journal.phase=TB_OTA_FAILED; save(); return status;
}
static uint64_t job_id(void) { return tb_u64(journal.manifest); }
bool tb_ota_pending_boot(void) {
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY;
}
bool tb_ota_busy(void) {
    return journal.phase==TB_OTA_RECEIVING || journal.phase==TB_OTA_VERIFIED || journal.phase==TB_OTA_ACTIVATED;
}
esp_err_t tb_ota_init(void) {
    esp_err_t err=nvs_flash_init_partition("ota_journal");
    if (err!=ESP_OK) return err;
    err=nvs_open_from_partition("ota_journal","update",NVS_READWRITE,&storage);
    if (err!=ESP_OK) return err;
    size_t size=sizeof journal;
    err=nvs_get_blob(storage,"job",&journal,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) { memset(&journal,0,sizeof journal); return ESP_OK; }
    if (err!=ESP_OK || size!=sizeof journal || journal.format!=1 || journal.phase>TB_OTA_FAILED ||
        !signature_valid(journal.manifest)) return ESP_ERR_INVALID_STATE;
    const esp_partition_t *running=esp_ota_get_running_partition();
    esp_partition_iterator_t iterator=esp_partition_find(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,NULL);
    destination=NULL;
    while (iterator) {
        const esp_partition_t *part=esp_partition_get(iterator);
        if (part->address==journal.address) { destination=part; break; }
        iterator=esp_partition_next(iterator);
    }
    esp_partition_iterator_release(iterator);
    uint32_t total=tb_u32(journal.manifest+12);
    const esp_partition_t *next=esp_ota_get_next_update_partition(NULL);
    bool inactive_target=next && next->address==journal.address;
    if (!destination || journal.offset>total || total>destination->size ||
        ((journal.phase==TB_OTA_RECEIVING || journal.phase==TB_OTA_VERIFIED ||
          (journal.phase==TB_OTA_ACTIVATED && running->address!=journal.address)) && !inactive_target) ||
        (journal.phase==TB_OTA_RECEIVING && (journal.offset&16383u)) ||
        ((journal.phase==TB_OTA_VERIFIED || journal.phase==TB_OTA_ACTIVATED || journal.phase==TB_OTA_CONFIRMED) &&
         journal.offset!=total)) return ESP_ERR_INVALID_STATE;
    received=journal.offset;
    if (journal.phase==TB_OTA_ACTIVATED && running->address!=journal.address) {
        journal.phase=TB_OTA_FAILED;
        if (!save()) return ESP_FAIL;
    }
    // A receiving job resumes only after a fresh authenticated BEGIN using the same signed manifest.
    return ESP_OK;
}
enum tb_status tb_ota_begin(const uint8_t *manifest, size_t size) {
    if (size!=TB_MANIFEST_SIGNED_SIZE || !tb_u64(manifest) || manifest[9] || tb_u16(manifest+10) ||
        !memchr(manifest+48,0,32) || !memchr(manifest+80,0,32)) return TB_INVALID;
    if (!signature_valid(manifest)) return TB_INTEGRITY;
    if (manifest[8]!=0) return TB_UNSUPPORTED;
    if (strcmp((const char *)manifest+80,TB_BOARD_ID)) return TB_INVALID;
    uint32_t total=tb_u32(manifest+12);
    if (total<sizeof(esp_app_desc_t)+32 || tb_ota_pending_boot()) return TB_STATE;
    if (tb_ota_busy()) {
        if (memcmp(journal.manifest,manifest,size)) return TB_BUSY;
        if (journal.phase==TB_OTA_VERIFIED || handle) return TB_OK;
        if (esp_ota_resume(destination,OTA_WITH_SEQUENTIAL_WRITES,received,&handle)!=ESP_OK) return TB_IO;
        return TB_OK;
    }
    const esp_partition_t *part=esp_ota_get_next_update_partition(NULL);
    if (!part || total>part->size) return TB_INVALID;
    memset(&journal,0,sizeof journal); journal.format=1; journal.phase=TB_OTA_RECEIVING; journal.address=part->address;
    memcpy(journal.manifest,manifest,size); destination=part; received=0;
    // Replace old VERIFIED intent durably before erasing any bytes of that slot.
    if (!save()) { journal.phase=TB_OTA_FAILED; return TB_IO; }
    if (esp_ota_begin(part,OTA_WITH_SEQUENTIAL_WRITES,&handle)!=ESP_OK) {
        journal.phase=TB_OTA_FAILED; save(); return TB_IO;
    }
    return TB_OK;
}
enum tb_status tb_ota_write(const uint8_t *data, size_t size) {
    if (size<=12 || size>TB_MAX_PAYLOAD || tb_u64(data)!=job_id()) return TB_INVALID;
    if (journal.phase!=TB_OTA_RECEIVING || !handle) return TB_STATE;
    uint32_t offset=tb_u32(data+8); size_t bytes=size-12;
    uint32_t total=tb_u32(journal.manifest+12);
    if (offset>total || bytes>total-offset) return TB_INVALID;
    if (offset<received) {
        if (bytes>received-offset) return TB_CONFLICT;
        if (esp_partition_read(destination,offset,scratch,bytes)!=ESP_OK) return TB_IO;
        return memcmp(scratch,data+12,bytes)?TB_CONFLICT:TB_OK;
    }
    if (offset!=received) return TB_CONFLICT;
    if (esp_ota_write(handle,data+12,bytes)!=ESP_OK) {
        esp_ota_abort(handle); handle=0; journal.phase=TB_OTA_FAILED; save(); return TB_IO;
    }
    received+=(uint32_t)bytes;
    uint32_t checkpoint=received&~16383u;
    if (checkpoint>journal.offset) {
        uint32_t previous=journal.offset; journal.offset=checkpoint;
        if (!save()) { journal.offset=previous; return TB_IO; }
    }
    return TB_OK;
}
enum tb_status tb_ota_finish(uint64_t id) {
    if (!id || id!=job_id()) return TB_INVALID;
    if (journal.phase==TB_OTA_VERIFIED) return save()?TB_OK:TB_IO;
    uint32_t total=tb_u32(journal.manifest+12);
    if (journal.phase!=TB_OTA_RECEIVING || !handle || received!=total) return TB_STATE;
    // Finalize buffered Flash writes before reading the complete candidate back.
    esp_err_t err=esp_ota_end(handle); handle=0;
    if (err!=ESP_OK) return failed(TB_INTEGRITY);
    psa_hash_operation_t operation=PSA_HASH_OPERATION_INIT;
    uint8_t digest[32]; size_t actual=0;
    if (psa_hash_setup(&operation,PSA_ALG_SHA_256)!=PSA_SUCCESS) return failed(TB_IO);
    for (uint32_t offset=0;offset<total;) {
        size_t bytes=total-offset; if (bytes>sizeof scratch) bytes=sizeof scratch;
        if (esp_partition_read(destination,offset,scratch,bytes)!=ESP_OK ||
            psa_hash_update(&operation,scratch,bytes)!=PSA_SUCCESS) { psa_hash_abort(&operation); return failed(TB_IO); }
        offset+=(uint32_t)bytes;
    }
    if (psa_hash_finish(&operation,digest,sizeof digest,&actual)!=PSA_SUCCESS || actual!=32) return failed(TB_IO);
    if (memcmp(digest,journal.manifest+16,32)) return failed(TB_INTEGRITY);
    esp_app_desc_t app;
    if (esp_ota_get_partition_description(destination,&app)!=ESP_OK ||
        strncmp(app.project_name,"tetherbench_probe_c5",sizeof app.project_name) ||
        strncmp(app.version,(const char *)journal.manifest+48,sizeof app.version)) return failed(TB_INTEGRITY);
    journal.phase=TB_OTA_VERIFIED; journal.offset=received;
    return save()?TB_OK:TB_IO;
}
enum tb_status tb_ota_activate(uint64_t id) {
    if (!id || id!=job_id() || journal.phase!=TB_OTA_VERIFIED || tb_ota_pending_boot()) return TB_STATE;
    journal.phase=TB_OTA_ACTIVATED;
    if (!save()) { journal.phase=TB_OTA_VERIFIED; return TB_IO; }
    if (esp_ota_set_boot_partition(destination)!=ESP_OK) {
        journal.phase=TB_OTA_VERIFIED; save(); return TB_IO;
    }
    return TB_OK;
}
enum tb_status tb_ota_abort(uint64_t id) {
    if (!id || id!=job_id()) return TB_INVALID;
    if (journal.phase==TB_OTA_ACTIVATED) return TB_STATE;
    if (handle) { esp_ota_abort(handle); handle=0; }
    if (nvs_erase_key(storage,"job")!=ESP_OK || nvs_commit(storage)!=ESP_OK) return TB_IO;
    memset(&journal,0,sizeof journal); destination=NULL; received=0; return TB_OK;
}
enum tb_status tb_ota_confirm(void) {
    if (tb_ota_pending_boot() && esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK) return TB_IO;
    if (journal.phase==TB_OTA_ACTIVATED && journal.address==esp_ota_get_running_partition()->address) {
        journal.phase=TB_OTA_CONFIRMED; return save()?TB_OK:TB_IO;
    }
    return TB_OK;
}
size_t tb_ota_status(uint8_t *out) {
    tb_put64(out,job_id()); tb_put32(out+8,journal.phase);
    tb_put32(out+12,received); tb_put32(out+16,journal.offset);
    tb_put32(out+20,tb_u32(journal.manifest+12));
    tb_put32(out+24,tb_ota_pending_boot());
    return 28;
}
