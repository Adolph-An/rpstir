#include "rpki-object/certificate.h"

#include <string.h>

#include "util/cryptlib_compat.h"
#include "util/hashutils.h"
#include "util/logging.h"
#include "rpki-asn1/cms.h"


struct Extension *find_extension(
    struct Extensions *extsp,
    const char *oid,
    bool create)
{
    struct Extension *extp;
    for (extp = (struct Extension *)member_casn(&extsp->self, 0);
         extp && diff_objid(&extp->extnID, oid);
         extp = (struct Extension *)next_of(&extp->self));
    if (!extp && create)
    {
        int num = num_items(&extsp->self);
        extp = (struct Extension *)inject_casn(&extsp->self, num);
        if (extp)
            write_objid(&extp->extnID, oid);
    }
    return extp;
}

struct Extension *make_extension(
    struct Extensions *extsp,
    const char *oid)
{
    struct Extension *extp = find_extension(extsp, oid, false);
    if (extp == NULL)
    {
        extp = (struct Extension *)inject_casn(&extsp->self,
                                               num_items(&extsp->self));
        if (extp == NULL)
        {
            return NULL;
        }
    }
    else
    {
        clear_casn(&extp->self);
    }

    write_objid(&extp->extnID, oid);

    return extp;
}

bool check_signature(
    struct casn *locertp,
    struct Certificate *hicertp,
    struct casn *sigp)
{
    CRYPT_CONTEXT pubkeyContext,
        hashContext;
    bool pubkeyContextInitialized = false;
    bool hashContextInitialized = false;
    CRYPT_PKCINFO_RSA rsakey;
    // CRYPT_KEYSET cryptKeyset;
    struct RSAPubKey rsapubkey;
    bool rsapubkeyInitialized = false;
    struct SignerInfo sigInfo;
    bool sigInfoInitialized = false;
    int bsize,
        sidsize;
    uchar *c,
       *buf = NULL,
        hash[40],
        sid[40];
    bool ret = true;
    int hash_length = sizeof(hash);

    // get SID and generate the sha-1 hash
    // (needed for cryptlib; see below)
    memset(sid, 0, 40);
    bsize = size_casn(&hicertp->toBeSigned.subjectPublicKeyInfo.self);
    if (bsize < 0)
    {
        LOG(LOG_ERR, "low cert size");
        ret = false;
        goto done;
    }
    buf = (uchar *) calloc(1, bsize);
    encode_casn(&hicertp->toBeSigned.subjectPublicKeyInfo.self, buf);
    sidsize = gen_hash(buf, bsize, sid, CRYPT_ALGO_SHA1);
    if (sidsize < 0)
    {
        LOG(LOG_ERR, "gen_hash failed");
        ret = false;
        goto done;
    }
    free(buf);
    buf = NULL;

    // generate the sha256 hash of the signed attributes. We don't call
    // gen_hash because we need the hashContext for later use (below).
    memset(hash, 0, 40);
    bsize = size_casn(locertp);
    if (bsize < 0)
    {
        LOG(LOG_ERR, "error sizing toBeSigned");
        ret = false;
        goto done;
    }
    buf = (uchar *) calloc(1, bsize);
    encode_casn(locertp, buf);

    // (re)init the crypt library
    if (cryptInit() != CRYPT_OK)
    {
        LOG(LOG_ERR, "error initializing cryptlib");
        ret = false;
        goto done;
    }

    if (cryptCreateContext(&hashContext, CRYPT_UNUSED, CRYPT_ALGO_SHA2) == CRYPT_OK)
    {
        hashContextInitialized = true;
    }
    else
    {
        LOG(LOG_ERR, "error creating hash context");
        ret = false;
        goto done;
    }

    cryptEncrypt(hashContext, buf, bsize);
    cryptEncrypt(hashContext, buf, 0);
    cryptGetAttributeString(hashContext, CRYPT_CTXINFO_HASHVALUE, hash, &hash_length);
    free(buf);

    // get the public key from the certificate and decode it into an RSAPubKey
    readvsize_casn(&hicertp->toBeSigned.subjectPublicKeyInfo.subjectPublicKey,
                   &c);
    RSAPubKey(&rsapubkey, 0);
    rsapubkeyInitialized = true;
    decode_casn(&rsapubkey.self, &c[1]);        // skip 1st byte (tag?) in BIT
                                                // STRING
    free(c);

    // set up the key by reading the modulus and exponent
    if (cryptCreateContext(&pubkeyContext, CRYPT_UNUSED, CRYPT_ALGO_RSA) == CRYPT_OK)
    {
        pubkeyContextInitialized = true;
    }
    else
    {
        LOG(LOG_ERR, "error creating pubkey context");
        ret = false;
        goto done;
    }

    cryptSetAttributeString(pubkeyContext, CRYPT_CTXINFO_LABEL, "label", 5);
    cryptInitComponents(&rsakey, CRYPT_KEYTYPE_PUBLIC);

    // read the modulus from rsapubkey
    bsize = readvsize_casn(&rsapubkey.modulus, &buf);
    c = buf;
    // if the first byte is a zero, skip it
    if (!*buf)
    {
        c++;
        bsize--;
    }
    cryptSetComponent((&rsakey)->n, c, bsize * 8);
    free(buf);
    buf = NULL;

    // read the exponent from the rsapubkey
    bsize = readvsize_casn(&rsapubkey.exponent, &buf);
    cryptSetComponent((&rsakey)->e, buf, bsize * 8);
    free(buf);
    buf = NULL;

    // set the modulus and exponent on the key
    cryptSetAttributeString(pubkeyContext, CRYPT_CTXINFO_KEY_COMPONENTS,
                            &rsakey, sizeof(CRYPT_PKCINFO_RSA));
    // all done with this now, free the storage
    cryptDestroyComponents(&rsakey);

    // make the structure cryptlib likes.
    // we discovered through detective work that cryptlib wants the
    // signature's SID field to be the sha-1 hash of the SID.
    SignerInfo(&sigInfo, (ushort) 0);   /* init sigInfo */
    sigInfoInitialized = true;
    write_casn_num(&sigInfo.version.self, 3);
    // copy_casn(&sigInfo.version.self, &sigInfop->version.self); /* copy over
    // */
    // copy_casn(&sigInfo.sid.self, &sigInfop->sid.self); /* copy over */
    write_casn(&sigInfo.sid.subjectKeyIdentifier, sid, sidsize);        /* sid
                                                                         * hash */

    // copy over digest algorithm, signature algorithm, signature
    write_objid(&sigInfo.digestAlgorithm.algorithm, id_sha256);
    write_casn(&sigInfo.digestAlgorithm.parameters.sha256, (uchar *) "", 0);
    write_objid(&sigInfo.signatureAlgorithm.algorithm,
                id_rsadsi_rsaEncryption);
    write_casn(&sigInfo.signatureAlgorithm.parameters.rsadsi_rsaEncryption,
               (uchar *) "", 0);
    uchar *sig;
    int siglth = readvsize_casn(sigp, &sig);
    write_casn(&sigInfo.signature, &sig[1], siglth - 1);
    free(sig);
    sig = NULL;

    // now encode as asn1, and check the signature
    bsize = size_casn(&sigInfo.self);
    buf = (uchar *) calloc(1, bsize);
    encode_casn(&sigInfo.self, buf);
    if (cryptCheckSignature(buf, bsize, pubkeyContext, hashContext) != CRYPT_OK)
    {
        LOG(LOG_DEBUG, "error checking signature");
        ret = false;
        goto done;
    }

done:
    free(buf);

    if (pubkeyContextInitialized)
        cryptDestroyContext(pubkeyContext);

    if (hashContextInitialized)
        cryptDestroyContext(hashContext);

    if (rsapubkeyInitialized)
        delete_casn(&rsapubkey.self);

    if (sigInfoInitialized)
        delete_casn(&sigInfo.self);

    return ret;
}

bool check_cert_signature(
    struct Certificate *locertp,
    struct Certificate *hicertp)
{
    return check_signature(&locertp->toBeSigned.self, hicertp,
                           &locertp->signature);
}

int writeHashedPublicKey(
    struct casn *valuep,
    struct casn *keyp,
    bool bad)
{
    uchar *bitval;
    int siz = readvsize_casn(keyp, &bitval);
    uchar hashbuf[24];
    siz = gen_hash(&bitval[1], siz - 1, hashbuf, CRYPT_ALGO_SHA1);
    free(bitval);
    if (bad)
        hashbuf[0]++;
    write_casn(valuep, hashbuf, siz);
    return siz;
}

/**
 * Trim trim_bit from the right of src and write it to dst as a bit string.
 */
static int write_rtrim_bitstring(
    struct casn * dst,
    const uint8_t * src,
    size_t src_length,
    bool trim_bit)
{
    const uint8_t trim_byte = trim_bit ? 0xff : 0x00;

    size_t prefix_bytes;
    for (prefix_bytes = src_length;
        prefix_bytes > 0 && src[prefix_bytes - 1] == trim_byte;
        --prefix_bytes);

    uint8_t unused_bits;
    for (unused_bits = 0;
        prefix_bytes > 0 && unused_bits < 8 &&
            (src[prefix_bytes - 1] & (1 << unused_bits)) ==
            (trim_byte & (1 << unused_bits));
        ++unused_bits);

    uint8_t * bitstring = malloc(prefix_bytes + 1);
    if (bitstring == NULL)
    {
        LOG(LOG_ERR, "out of memory");
        return -1;
    }

    memcpy(&bitstring[1], src, prefix_bytes);
    bitstring[0] = unused_bits;
    bitstring[prefix_bytes] &= 0xff << unused_bits;

    int ret = write_casn(dst, bitstring, prefix_bytes + 1);

    free(bitstring);

    return ret;
}

bool make_IPAddrOrRange(
    struct IPAddressOrRangeA *ipAddrOrRangep,
    int af,
    const void * low,
    const void * high)
{
    static const uint8_t zeroes[16] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    static const uint8_t ones[16] = {
        0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
    };

    const uint8_t * low_bytes = (const uint8_t *)low;
    const uint8_t * high_bytes = (const uint8_t *)high;
    size_t addr_length;

    switch (af)
    {
    case AF_INET:
        addr_length = 4;
        break;

    case AF_INET6:
        addr_length = 16;
        break;

    default:
        LOG(LOG_ERR, "invalid address family %d", af);
        return false;
    }

    if (memcmp(low, high, addr_length) > 0)
    {
        LOG(LOG_ERR, "low > high");
        return false;
    }

    // compute the length of the common prefix
    size_t common_bytes;
    size_t common_bits;
    size_t noncommon_byte_offset; // first byte that has no common prefix
    for (common_bytes = 0;
        common_bytes < addr_length &&
            low_bytes[common_bytes] == high_bytes[common_bytes];
        ++common_bytes);
    for (common_bits = 0;
        common_bytes < addr_length &&
            (low_bytes[common_bytes] & (0x80 >> common_bits)) ==
            (high_bytes[common_bytes] & (0x80 >> common_bits));
        ++common_bits);
    noncommon_byte_offset = common_bytes;
    if (common_bits > 0)
    {
        ++noncommon_byte_offset;
    }

    // determine whether it's a prefix or a range
    bool is_prefix;
    if (common_bytes == addr_length)
    {
        // low == high
        is_prefix = true;
    }
    else if (common_bits > 0 &&
        (low_bytes[common_bytes] & (0xff >> common_bits)) != 0x00)
    {
        // low isn't all zeroes after the common prefix
        is_prefix = false;
    }
    else if (common_bits > 0 &&
        (high_bytes[common_bytes] & (0xff >> common_bits)) !=
            (0xff >> common_bits))
    {
        // high isn't all ones after the common prefix
        is_prefix = false;
    }
    else if (noncommon_byte_offset == addr_length)
    {
        // The common prefix goes into the last byte and the last bits of the
        // last bytes are appropriately zero or one.
        is_prefix = true;
    }
    else if (memcmp(&low_bytes[noncommon_byte_offset], zeroes,
                    addr_length - noncommon_byte_offset) != 0)
    {
        // the trailing bytes of low are not all zero
        is_prefix = false;
    }
    else if (memcmp(&high_bytes[noncommon_byte_offset], ones,
                    addr_length - noncommon_byte_offset) != 0)
    {
        // the trailing bytes of high are not all ones
        is_prefix = false;
    }
    else
    {
        is_prefix = true;
    }

    if (is_prefix)
    {
        // create BIT STRING
        uint8_t prefix[noncommon_byte_offset + 1];
        memcpy(&prefix[1], low, noncommon_byte_offset);
        prefix[0] = (8 - common_bits) % 8;

        if (write_casn(&ipAddrOrRangep->addressPrefix,
                       prefix, sizeof(prefix)) <= 0)
        {
            LOG(LOG_ERR, "failed to write prefix");
            return false;
        }
    }
    else
    {
        if (write_rtrim_bitstring(&ipAddrOrRangep->addressRange.min,
                                  low_bytes, addr_length, 0) <= 0)
        {
            LOG(LOG_ERR, "failed to write the low end of the range");
            return false;
        }

        if (write_rtrim_bitstring(&ipAddrOrRangep->addressRange.max,
                                  high_bytes, addr_length, 1) <= 0)
        {
            LOG(LOG_ERR, "failed to write the high end of the range");
            return false;
        }

    }

    return true;
}

bool has_non_inherit_resources(
    struct Certificate *cert)
{
    struct Extension *ext;

    ext = find_extension(&cert->toBeSigned.extensions, id_pe_ipAddrBlock,
                         false);
    if (ext)
    {
        struct IpAddrBlock *ip_ext = &ext->extnValue.ipAddressBlock;
        struct IPAddressFamilyA *family;
        for (family = (struct IPAddressFamilyA *)member_casn(&ip_ext->self, 0);
             family != NULL;
             family = (struct IPAddressFamilyA *)next_of(&family->self))
        {
            if (size_casn(&family->ipAddressChoice.inherit) <= 0)
            {
                return true;
            }
        }
    }

    ext = find_extension(&cert->toBeSigned.extensions, id_pe_autonomousSysNum,
                         false);
    if (ext)
    {
        struct ASNum *as_ext = &ext->extnValue.autonomousSysNum;

        if (size_casn(&as_ext->asnum.self) > 0 &&
            size_casn(&as_ext->asnum.inherit) <= 0)
        {
            return true;
        }

        if (size_casn(&as_ext->rdi.self) > 0 &&
            size_casn(&as_ext->rdi.inherit) <= 0)
        {
            return true;
        }
    }

    return false;
}
