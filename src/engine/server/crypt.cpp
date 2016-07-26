#ifdef CONF_SQL
#include <stdint.h>
#include <base/system.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
 
//PBKDF2_HMAC_SHA_512
void Crypt(const char* pass, const unsigned char* salt, int32_t iterations, uint32_t outputBytes, char* hexResult)
{
	unsigned int i;
	unsigned char digest[outputBytes];
	PKCS5_PBKDF2_HMAC(pass, str_length((char*)pass), salt, str_length((char*)salt), iterations, EVP_sha512(), outputBytes, digest);
	for (i = 0; i < sizeof(digest); i++)
	{
		sprintf(hexResult + (i * 2), "%02x", 255 & digest[i]);
	}
}  

#endif
