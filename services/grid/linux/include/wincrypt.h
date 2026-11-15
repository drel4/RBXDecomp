#pragma once
#include "windows.h"
#include <openssl/evp.h>
typedef int HCRYPTPROV;
typedef EVP_MD_CTX* HCRYPTHASH;
#define PROV_RSA_FULL 1
#define CRYPT_VERIFYCONTEXT 0
#define CALG_MD5 1
#define HP_HASHSIZE 1
#define HP_HASHVAL 2
inline BOOL CryptAcquireContext(HCRYPTPROV* p,const char*,const char*,DWORD,DWORD) { *p=1; return TRUE; }
inline BOOL CryptCreateHash(HCRYPTPROV,DWORD,HCRYPTPROV,DWORD,HCRYPTHASH* h) { *h=EVP_MD_CTX_new(); return *h && EVP_DigestInit_ex(*h,EVP_md5(),NULL); }
inline BOOL CryptHashData(HCRYPTHASH h,const BYTE* data,DWORD n,DWORD) { return EVP_DigestUpdate(h,data,n); }
inline BOOL CryptGetHashParam(HCRYPTHASH h,DWORD kind,BYTE* out,DWORD* length,DWORD) {
  if(kind==HP_HASHSIZE) { *(DWORD*)out=16; *length=sizeof(DWORD); return TRUE; }
  unsigned size=0; if(*length<16) return FALSE;
  EVP_MD_CTX* copy=EVP_MD_CTX_new(); if(!copy) return FALSE;
  BOOL ok=EVP_MD_CTX_copy_ex(copy,h) && EVP_DigestFinal_ex(copy,out,&size);
  EVP_MD_CTX_free(copy); *length=size; return ok;
}
inline BOOL CryptDestroyHash(HCRYPTHASH h) { EVP_MD_CTX_free(h); return TRUE; }
inline BOOL CryptReleaseContext(HCRYPTPROV,DWORD) { return TRUE; }
