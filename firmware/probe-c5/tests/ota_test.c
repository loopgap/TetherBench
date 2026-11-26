// Test persistence and SDK error paths, with deterministic crypto and Flash doubles.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../components/probe_ota/probe_ota.c"

static const esp_partition_t slots[] = {{0x20000, 65536}, {0x200000, 65536}};
static unsigned current_slot, selected_slot, writes, begins, native_offset;
static bool pending, persisted, fail_save, fail_write, fail_delete, valid_signature = true;
static bool finalized;
static uint8_t durable[1024], flash_data[65536];
static size_t durable_size;

esp_err_t nvs_flash_init_partition(const char *p)
{
    (void)p;
    return ESP_OK;
}
esp_err_t nvs_open_from_partition(const char *p, const char *n, int m, nvs_handle_t *h)
{
    (void)p;
    (void)n;
    (void)m;
    *h = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *n)
{
    (void)h;
    (void)k;
    if (!persisted)
        return ESP_ERR_NVS_NOT_FOUND;
    assert(*n >= durable_size);
    memcpy(out, durable, durable_size);
    *n = durable_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *in, size_t n)
{
    (void)h;
    (void)k;
    if (fail_save)
        return ESP_FAIL;
    assert(n <= sizeof durable);
    memcpy(durable, in, n);
    durable_size = n;
    persisted = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *k)
{
    (void)h;
    (void)k;
    if (fail_delete)
        return ESP_FAIL;
    persisted = false;
    return ESP_OK;
}
const esp_partition_t *esp_ota_get_running_partition(void)
{
    return slots + current_slot;
}
esp_partition_iterator_t esp_partition_find(int t, int s, const char *n)
{
    (void)t;
    (void)s;
    (void)n;
    return slots;
}
const esp_partition_t *esp_partition_get(esp_partition_iterator_t it)
{
    return it;
}
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it)
{
    return it == slots ? slots + 1 : NULL;
}
void esp_partition_iterator_release(esp_partition_iterator_t it)
{
    (void)it;
}
esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *out, size_t n)
{
    assert(p != slots + current_slot && off + n <= sizeof flash_data);
    memcpy(out, flash_data + off, n);
    return ESP_OK;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *s)
{
    (void)p;
    *s = pending ? 1 : 0;
    return ESP_OK;
}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{
    (void)p;
    return slots + (1 - current_slot);
}
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t n, esp_ota_handle_t *h)
{
    (void)n;
    assert(p != slots + current_slot && persisted);
    assert(tb_u32(durable + 4) == TB_OTA_RECEIVING);
    memset(flash_data, 255, sizeof flash_data);
    native_offset = 0;
    begins++;
    finalized = false;
    *h = 1;
    return ESP_OK;
}
esp_err_t esp_ota_resume(const esp_partition_t *p, size_t e, size_t off, esp_ota_handle_t *h)
{
    (void)e;
    assert(p != slots + current_slot && off <= sizeof flash_data);
    native_offset = (unsigned)off;
    finalized = false;
    *h = 1;
    return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *p, size_t n)
{
    assert(h && native_offset + n <= sizeof flash_data);
    if (fail_write)
        return ESP_FAIL;
    memcpy(flash_data + native_offset, p, n);
    native_offset += (unsigned)n;
    writes++;
    return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t h)
{
    assert(h);
    finalized = true;
    return ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t h)
{
    assert(h);
    return ESP_OK;
}
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *app)
{
    (void)p;
    memset(app, 0, sizeof *app);
    strcpy(app->version, "test");
    strcpy(app->project_name, "tetherbench_probe_c5");
    return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{
    selected_slot = (unsigned)(p - slots);
    return ESP_OK;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    pending = false;
    return ESP_OK;
}
void psa_set_key_type(psa_key_attributes_t *p, unsigned v)
{
    (void)p;
    (void)v;
}
void psa_set_key_bits(psa_key_attributes_t *p, unsigned v)
{
    (void)p;
    (void)v;
}
void psa_set_key_usage_flags(psa_key_attributes_t *p, unsigned v)
{
    (void)p;
    (void)v;
}
void psa_set_key_algorithm(psa_key_attributes_t *p, unsigned v)
{
    (void)p;
    (void)v;
}
int psa_import_key(const psa_key_attributes_t *a, const uint8_t *d, size_t n, mbedtls_svc_key_id_t *id)
{
    (void)a;
    (void)d;
    (void)n;
    *id = 1;
    return 0;
}
int psa_destroy_key(mbedtls_svc_key_id_t id)
{
    (void)id;
    return 0;
}
int psa_hash_compute(unsigned a, const uint8_t *d, size_t n, uint8_t *out, size_t cap, size_t *actual)
{
    (void)a;
    (void)d;
    (void)n;
    assert(cap >= 32);
    memset(out, 0x5a, 32);
    *actual = 32;
    return 0;
}
int psa_verify_hash(mbedtls_svc_key_id_t id, unsigned a, const uint8_t *d, size_t n, const uint8_t *s, size_t sn)
{
    (void)id;
    (void)a;
    (void)d;
    (void)n;
    (void)s;
    assert(sn == 64);
    return valid_signature ? 0 : -1;
}
int psa_hash_setup(psa_hash_operation_t *op, unsigned a)
{
    (void)op;
    (void)a;
    assert(finalized);
    return 0;
}
int psa_hash_update(psa_hash_operation_t *op, const uint8_t *p, size_t n)
{
    (void)op;
    (void)p;
    (void)n;
    return 0;
}
int psa_hash_abort(psa_hash_operation_t *op)
{
    (void)op;
    return 0;
}
int psa_hash_finish(psa_hash_operation_t *op, uint8_t *out, size_t cap, size_t *actual)
{
    (void)op;
    assert(cap >= 32);
    memset(out, 0x5a, 32);
    *actual = 32;
    return 0;
}
static void reboot(void)
{
    memset(&journal, 0, sizeof journal);
    handle = 0;
    destination = NULL;
    received = 0;
    assert(tb_ota_init() == ESP_OK);
}
static void reset_runtime(void)
{
    memset(&journal, 0, sizeof journal);
    handle = 0;
    destination = NULL;
    received = 0;
}
static void manifest(uint8_t out[176], uint64_t id)
{
    memset(out, 0, 176);
    tb_put64(out, id);
    tb_put32(out + 12, 40000);
    memset(out + 16, 0x5a, 32);
    strcpy((char *)out + 48, "test");
    strcpy((char *)out + 80, TB_BOARD_ID);
}
static void write_to(uint64_t id, uint32_t end)
{
    uint8_t block[12 + 2000];
    memset(block + 12, 0x33, 2000);
    tb_put64(block, id);
    while (received < end) {
        size_t n = end - received;
        if (n > 2000)
            n = 2000;
        tb_put32(block + 8, received);
        assert(tb_ota_write(block, n + 12) == TB_OK);
    }
}
int main(void)
{
    uint8_t m[176], block[112];
    manifest(m, 1);
    reboot();
    valid_signature = false;
    assert(tb_ota_begin(m, 176) == TB_INTEGRITY && !begins);
    valid_signature = true;
    fail_save = true;
    assert(tb_ota_begin(m, 176) == TB_IO && !begins);
    fail_save = false;
    assert(tb_ota_begin(m, 176) == TB_OK);
    uint32_t target = tb_u32(durable + 12);
    tb_put32(durable + 12, slots[current_slot].address);
    reset_runtime();
    assert(tb_ota_init() == ESP_ERR_INVALID_STATE);
    tb_put32(durable + 12, target);
    tb_put32(durable + 8, 1);
    reset_runtime();
    assert(tb_ota_init() == ESP_ERR_INVALID_STATE);
    tb_put32(durable + 8, 0);
    reboot();
    assert(tb_ota_begin(m, 176) == TB_OK);
    write_to(1, 20000);
    assert(journal.offset == 16384);
    unsigned before = writes;
    memset(block + 12, 0x33, 100);
    tb_put64(block, 1);
    tb_put32(block + 8, 19000);
    assert(tb_ota_write(block, sizeof block) == TB_OK && writes == before);
    block[12] ^= 1;
    assert(tb_ota_write(block, sizeof block) == TB_CONFLICT);
    reboot();
    assert(received == 16384 && !handle);
    assert(tb_ota_begin(m, 176) == TB_OK);
    write_to(1, 40000);
    assert(tb_ota_finish(1) == TB_OK && journal.phase == TB_OTA_VERIFIED);
    manifest(m, 2);
    assert(tb_ota_begin(m, 176) == TB_BUSY);
    assert(tb_ota_activate(1) == TB_OK && selected_slot == 1);
    assert(tb_ota_abort(1) == TB_STATE);
    current_slot = 1;
    pending = true;
    reboot();
    assert(tb_ota_pending_boot());
    assert(tb_ota_begin(m, 176) == TB_STATE);
    assert(tb_ota_confirm() == TB_OK && journal.phase == TB_OTA_CONFIRMED && !pending);
    assert(tb_ota_begin(m, 176) == TB_OK);
    assert(tb_ota_begin(m, 175) == TB_INVALID);
    fail_write = true;
    tb_put64(block, 2);
    tb_put32(block + 8, 0);
    assert(tb_ota_write(block, sizeof block) == TB_IO && !handle && journal.phase == TB_OTA_FAILED);
    fail_write = false;
    fail_delete = true;
    assert(tb_ota_abort(2) == TB_IO && job_id() == 2);
    fail_delete = false;
    assert(tb_ota_abort(2) == TB_OK);
    reboot();
    assert(journal.phase == TB_OTA_EMPTY);
    manifest(m, 3);
    assert(tb_ota_begin(m, 176) == TB_OK);
    write_to(3, 40000);
    journal.manifest[16] ^= 1;
    assert(tb_ota_finish(3) == TB_INTEGRITY && journal.phase == TB_OTA_FAILED);
    puts("OTA: SDK/crypto doubles verified persistence ordering, restart, duplicate blocks and failures; not "
         "cryptographic or power-cut proof");
}
