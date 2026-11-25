#pragma once
#include <stddef.h>
#include <stdint.h>
typedef unsigned psa_key_attributes_t;
typedef unsigned mbedtls_svc_key_id_t;
typedef unsigned psa_hash_operation_t;
#define PSA_KEY_ATTRIBUTES_INIT 0
#define PSA_HASH_OPERATION_INIT 0
#define PSA_SUCCESS 0
#define PSA_ECC_FAMILY_SECP_R1 1
#define PSA_KEY_USAGE_VERIFY_HASH 1
#define PSA_ALG_SHA_256 1
#define PSA_KEY_TYPE_ECC_PUBLIC_KEY(x) (x)
#define PSA_ALG_ECDSA(x) (x)
void psa_set_key_type(psa_key_attributes_t *p,unsigned value);
void psa_set_key_bits(psa_key_attributes_t *p,unsigned value);
void psa_set_key_usage_flags(psa_key_attributes_t *p,unsigned value);
void psa_set_key_algorithm(psa_key_attributes_t *p,unsigned value);
int psa_import_key(const psa_key_attributes_t *a,const uint8_t *data,size_t size,mbedtls_svc_key_id_t *id);
int psa_destroy_key(mbedtls_svc_key_id_t id);
int psa_hash_compute(unsigned algorithm,const uint8_t *data,size_t size,uint8_t *out,size_t cap,size_t *actual);
int psa_verify_hash(mbedtls_svc_key_id_t id,unsigned alg,const uint8_t *digest,size_t size,const uint8_t *signature,size_t signature_size);
int psa_hash_setup(psa_hash_operation_t *op,unsigned alg);
int psa_hash_update(psa_hash_operation_t *op,const uint8_t *data,size_t size);
int psa_hash_abort(psa_hash_operation_t *op);
int psa_hash_finish(psa_hash_operation_t *op,uint8_t *out,size_t cap,size_t *actual);
