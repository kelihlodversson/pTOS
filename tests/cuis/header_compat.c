/* Transitional native bindings can coexist with standard GEM selectors. */
#if LEGACY_HEADER_FIRST
#include "aes_client.h"
#include "gem.h"
#else
#include "gem.h"
#include "aes_client.h"
#endif
int main(void)
{
    AES_CLIENT_MESSAGE message = { 0 };
    return AES_MESSAGE != 12 || sizeof(message) != 16;
}
