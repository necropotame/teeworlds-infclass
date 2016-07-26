#ifdef CONF_SQL
#ifndef ENGINE_SERVER_BCRYPT_H
#define ENGINE_SERVER_BCRYPT_H

void Crypt(const char* pass, const unsigned char* salt, int32_t iterations, uint32_t outputBytes, char* hexResult);

#endif
#endif
