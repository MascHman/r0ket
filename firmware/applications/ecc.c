/* 
  This program implements the ECIES public key encryption scheme based on the
  NIST B163 elliptic curve and the XTEA block cipher. The code was written
  as an accompaniment for an article published in phrack #63 and is released to
  the public domain.
  Original author: Phrack Staff
  Ported to ARM7TDMI: Jiri Pittner <jiri@pittnerovi.com>
  compiled by arm-elf-gcc (GCC) 4.0.1 and tested on LPC2106
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "basic/basic.h"
#include "lcd/render.h"
#include "lcd/allfonts.h"


static int xrandm=100000000;
static int xrandm1=10000;
static int xrandb1=51723621;

int xmult(int p,int q)
{
int p1,p0,q1,q0;

p1=p/xrandm1; p0=p%xrandm1;
q1=q/xrandm1; q0=q%xrandm1;
return (((p0*q1+p1*q0)%xrandm1)*xrandm1+p0*q0)%xrandm;
}

unsigned char rnd1()
{
static int a=123456789;
a = (xmult(a,xrandb1)+1)%xrandm;
return a & 0xff;
}



#define MACRO(A) do { A; } while(0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define NO_HTONL

#ifndef NO_HTONL
#define INT2CHARS(ptr, val) MACRO( *(uint32_t*)(ptr) = htonl(val) )
#define CHARS2INT(ptr) ntohl(*(uint32_t*)(ptr))
#else


#if 1

//compiles to a quite reasonable assembly code

void INT2CHARS (unsigned char *ptr, uint32_t val) 
{
*ptr++ =val; val>>=8;
*ptr++ =val; val>>=8;
*ptr++ =val; val>>=8;
*ptr++ =val;
}

uint32_t CHARS2INT(const unsigned char *ptr)
{
uint32_t r;
ptr+=3;
r=*ptr--; r<<=8;
r|=*ptr--; r<<=8;
r|=*ptr--; r<<=8;
r|=*ptr--; 
return r;
}

#else

/* ARM architecture has a problem with non-word-aligned addresses
   the load/store of a 32-bit register behaves very counterintuitively in such a case

void INT2CHARS (char *ptr, const uint32_t val)
{
uint32_t *p = (uint32_t *)ptr;
*p=val;
}

uint32_t CHARS2INT(const char *ptr)
{
uint32_t *p = (uint32_t *)ptr;
return *p;
}
*/

/* this has the same problem */
#define INT2CHARS(ptr, val) MACRO( *(uint32_t*)(ptr) = (uint32_t)val )
#define CHARS2INT(ptr) (*(uint32_t*)(ptr))

#endif


#endif



/******************************************************************************/

#define DEGREE 163                      /* the degree of the field polynomial */
#define MARGIN 3                                          /* don't touch this */
#define NUMWORDS ((DEGREE + MARGIN + 31) / 32)

   /* the following type will represent bit vectors of length (DEGREE+MARGIN) */
typedef uint32_t bitstr_t[NUMWORDS];

     /* some basic bit-manipulation routines that act on these vectors follow */
#define bitstr_getbit(A, idx) ((A[(idx) / 32] >> ((idx) % 32)) & 1)
#define bitstr_setbit(A, idx) MACRO( A[(idx) / 32] |= 1 << ((idx) % 32) )
#define bitstr_clrbit(A, idx) MACRO( A[(idx) / 32] &= ~(1 << ((idx) % 32)) )

#define bitstr_clear(A) MACRO( memset(A, 0, sizeof(bitstr_t)) )
#define bitstr_copy(A, B) MACRO( memcpy(A, B, sizeof(bitstr_t)) )
#define bitstr_swap(A, B) MACRO( bitstr_t h; \
  bitstr_copy(h, A); bitstr_copy(A, B); bitstr_copy(B, h) )
#define bitstr_is_equal(A, B) (! memcmp(A, B, sizeof(bitstr_t)))

int bitstr_is_clear(const bitstr_t x)
{
  int i;
  for(i = 0; i < NUMWORDS && ! *x++; i++);
  return i == NUMWORDS;
}

                              /* return the number of the highest one-bit + 1 */
int bitstr_sizeinbits(const bitstr_t x)
{
  int i;
  uint32_t mask;
  for(x += NUMWORDS, i = 32 * NUMWORDS; i > 0 && ! *--x; i -= 32);
  if (i)
    for(mask = ((uint32_t) 1) << 31; ! (*x & mask); mask >>= 1, i--);
  return i;
}

                                              /* left-shift by 'count' digits */
void bitstr_lshift(bitstr_t A, const bitstr_t B, int count)
{
  int i, offs = 4 * (count / 32);
  memmove((void*)A + offs, B, sizeof(bitstr_t) - offs);
  memset(A, 0, offs);
  if (count %= 32) {
    for(i = NUMWORDS - 1; i > 0; i--)
      A[i] = (A[i] << count) | (A[i - 1] >> (32 - count));
    A[0] <<= count;
  }
}

                                            /* (raw) import from a byte array */
void bitstr_import(bitstr_t x, const char *s)
{
  int i;
  for(x += NUMWORDS, i = 0; i < NUMWORDS; i++, s += 4)
    *--x = CHARS2INT(s);
}

                                              /* (raw) export to a byte array */
void bitstr_export(char *s, const bitstr_t x)
{
  int i;
  for(x += NUMWORDS, i = 0; i < NUMWORDS; i++, s += 4)
    INT2CHARS(s, *--x);
}

                                   /* export as hex string (null-terminated!) */
void bitstr_to_hex(char *s, const bitstr_t x)
{
  int i;
  for(x += NUMWORDS, i = 0; i < NUMWORDS; i++, s += 8)
    siprintf(s, "%08lx", *--x);
}


uint8_t letter2bin (const char c)
{
return c>'9' ? c+10-(c>='a'?'a':'A') : c-'0';
}

uint8_t octet2bin(const char* octet)
{
return (letter2bin(octet[0])<<4) | letter2bin(octet[1]);
}

void bin2letter(char *c, uint8_t b)
{
*c = b<10? '0'+b : 'A'+b-10;
}

void bin2octet(char *octet, uint8_t bin)
{
bin2letter(octet,bin>>4);
bin2letter(octet+1,bin&0x0f);
}


uint32_t getword32(const char *s)
{
//little endian
union {uint32_t i; uint8_t c[sizeof(uint32_t)];} r;
r.c[3]=octet2bin(s);
r.c[2]=octet2bin(s+2);
r.c[1]=octet2bin(s+4);
r.c[0]=octet2bin(s+6);
return r.i;
}
                                                  /* import from a hex string */
int bitstr_parse(bitstr_t x, const char *s)
{
  int len = strlen(s);
  //if ((s[len = strspn(s, "0123456789abcdefABCDEF")]) ||
  //    (len > NUMWORDS * 8))
  //  return -1;

  bitstr_clear(x);
  x += len / 8;
  if (len % 8) {
    *x=getword32(s);
    *x >>= 32 - 4 * (len % 8);
    s += len % 8;
    len &= ~7;
  }
  for(; *s; s += 8)
	*--x = getword32(s);
  return len;
}

/******************************************************************************/

typedef bitstr_t elem_t;           /* this type will represent field elements */

elem_t poly;                                      /* the reduction polynomial */

#define field_set1(A) MACRO( A[0] = 1; memset(A + 1, 0, sizeof(elem_t) - 4) )

int field_is1(const elem_t x)
{
  int i;
  if (*x++ != 1) return 0;
  for(i = 1; i < NUMWORDS && ! *x++; i++);
  return i == NUMWORDS;
}

void field_add(elem_t z, const elem_t x, const elem_t y)    /* field addition */
{
  int i;
  for(i = 0; i < NUMWORDS; i++)
    *z++ = *x++ ^ *y++;
}

#define field_add1(A) MACRO( A[0] ^= 1 )

                                                      /* field multiplication */
void field_mult(elem_t z, const elem_t x, const elem_t y)
{
  elem_t b;
  int i, j;
  /* assert(z != y); */
  bitstr_copy(b, x);
  if (bitstr_getbit(y, 0))
    bitstr_copy(z, x);
  else
    bitstr_clear(z);
  for(i = 1; i < DEGREE; i++) {
    for(j = NUMWORDS - 1; j > 0; j--)
      b[j] = (b[j] << 1) | (b[j - 1] >> 31);
    b[0] <<= 1;
    if (bitstr_getbit(b, DEGREE))
      field_add(b, b, poly);
    if (bitstr_getbit(y, i))
      field_add(z, z, b);
  }
}

void field_invert(elem_t z, const elem_t x)                /* field inversion */
{
  elem_t u, v, g, h;
  int i;
  bitstr_copy(u, x);
  bitstr_copy(v, poly);
  bitstr_clear(g);
  field_set1(z);
  while (! field_is1(u)) {
    i = bitstr_sizeinbits(u) - bitstr_sizeinbits(v);
    if (i < 0) {
      bitstr_swap(u, v); bitstr_swap(g, z); i = -i;
    }
    bitstr_lshift(h, v, i);
    field_add(u, u, h);
    bitstr_lshift(h, g, i);
    field_add(z, z, h);
  }
}

/******************************************************************************/

/* The following routines do the ECC arithmetic. Elliptic curve points
   are represented by pairs (x,y) of elem_t. It is assumed that curve
   coefficient 'a' is equal to 1 (this is the case for all NIST binary
   curves). Coefficient 'b' is given in 'coeff_b'.  '(base_x, base_y)'
   is a point that generates a large prime order group.             */

elem_t coeff_b, base_x, base_y;

#define point_is_zero(x, y) (bitstr_is_clear(x) && bitstr_is_clear(y))
#define point_set_zero(x, y) MACRO( bitstr_clear(x); bitstr_clear(y) )
#define point_copy(x1, y1, x2, y2) MACRO( bitstr_copy(x1, x2); \
                                          bitstr_copy(y1, y2) )

                           /* check if y^2 + x*y = x^3 + *x^2 + coeff_b holds */
int is_point_on_curve(const elem_t x, const elem_t y)
{
  elem_t a, b;
  if (point_is_zero(x, y))
    return 1;
  field_mult(a, x, x);
  field_mult(b, a, x);
  field_add(a, a, b);
  field_add(a, a, coeff_b);
  field_mult(b, y, y);
  field_add(a, a, b);
  field_mult(b, x, y);
  return bitstr_is_equal(a, b);
}

void point_double(elem_t x, elem_t y)               /* double the point (x,y) */
{
  if (! bitstr_is_clear(x)) {
    elem_t a;
    field_invert(a, x);
    field_mult(a, a, y);
    field_add(a, a, x);
    field_mult(y, x, x);
    field_mult(x, a, a);
    field_add1(a);        
    field_add(x, x, a);
    field_mult(a, a, x);
    field_add(y, y, a);
  }
  else
    bitstr_clear(y);
}

                   /* add two points together (x1, y1) := (x1, y1) + (x2, y2) */
void point_add(elem_t x1, elem_t y1, const elem_t x2, const elem_t y2)
{
  if (! point_is_zero(x2, y2)) {
    if (point_is_zero(x1, y1))
      point_copy(x1, y1, x2, y2);
    else {
      if (bitstr_is_equal(x1, x2)) {
	if (bitstr_is_equal(y1, y2))
	  point_double(x1, y1);
	else 
	  point_set_zero(x1, y1);
      }
      else {
	elem_t a, b, c, d;
	field_add(a, y1, y2);
	field_add(b, x1, x2);
	field_invert(c, b);
	field_mult(c, c, a);
	field_mult(d, c, c);
	field_add(d, d, c);
	field_add(d, d, b);
	field_add1(d);
	field_add(x1, x1, d);
	field_mult(a, x1, c);
	field_add(a, a, d);
	field_add(y1, y1, a);
	bitstr_copy(x1, d);
      }
    }
  }
}

/******************************************************************************/

typedef bitstr_t exp_t;

exp_t base_order;

                         /* point multiplication via double-and-add algorithm */
void point_mult(elem_t x, elem_t y, const exp_t exp)
{
  elem_t X, Y;
  int i;
  point_set_zero(X, Y);
  for(i = bitstr_sizeinbits(exp) - 1; i >= 0; i--) {
    point_double(X, Y);
    if (bitstr_getbit(exp, i))
      point_add(X, Y, x, y);
  }
  point_copy(x, y, X, Y);
}

                               /* draw a random value 'exp' with 1 <= exp < n */
//@@@ Make a HARDWARE randomness generator with ARM, at the moment just a simple pseudorandom replacement
void get_random_exponent(exp_t exp)
{
  unsigned char buf[4 * NUMWORDS];
  int r ;
  do {
for(r=0; r<4 * NUMWORDS; ++r)
	{
	buf[r]= rnd1();
	}
    bitstr_import(exp, buf);
    for(r = bitstr_sizeinbits(base_order) - 1; r < NUMWORDS * 32; r++)
      bitstr_clrbit(exp, r);
  } while(bitstr_is_clear(exp));
}

/******************************************************************************/

void XTEA_init_key(uint32_t *k, const char *key)
{
  k[0] = CHARS2INT(key + 0); k[1] = CHARS2INT(key + 4);
  k[2] = CHARS2INT(key + 8); k[3] = CHARS2INT(key + 12);
}

                                                     /* the XTEA block cipher */
void XTEA_encipher_block(char *data, const uint32_t *k)
{
  uint32_t sum = 0, delta = 0x9e3779b9, y, z;
  int i;
  y = CHARS2INT(data); z = CHARS2INT(data + 4);
  for(i = 0; i < 32; i++) {
    y += ((z << 4 ^ z >> 5) + z) ^ (sum + k[sum & 3]);
    sum += delta;
    z += ((y << 4 ^ y >> 5) + y) ^ (sum + k[(sum >> 11) & 3]);
  }
  INT2CHARS(data, y); INT2CHARS(data + 4, z);
}
                                                       /* encrypt in CTR mode */

void XTEA_ctr_crypt(char *data, int size, const char *key) 
{
  uint32_t k[4], ctr = 0;
  int len, i;
  char buf[8];
  XTEA_init_key(k, key);
  while(size) {
    INT2CHARS(buf, 0); INT2CHARS(buf + 4, ctr++);
    XTEA_encipher_block(buf, k);
    len = MIN(8, size);
    for(i = 0; i < len; i++)
      *data++ ^= buf[i];
    size -= len;
  }
}

                                                     /* calculate the CBC MAC */
void XTEA_cbcmac(char *mac, const char *data, int size, const char *key)
{
  uint32_t k[4];
  int len, i;
  XTEA_init_key(k, key);
  
  INT2CHARS(mac, 0L);
  INT2CHARS(mac + 4, size);
  XTEA_encipher_block(mac, k);
  while(size) {
    len = MIN(8, size);
    for(i = 0; i < len; i++)
      mac[i] ^= *data++;
    XTEA_encipher_block(mac, k);
    size -= len;
  }
}

                                     /* modified(!) Davies-Meyer construction.*/
void XTEA_davies_meyer(char *out, const char *in, int ilen)
{
  uint32_t k[4];
  char buf[8];
  int i;
  memset(out, 0, 8);
  while(ilen--) {
    XTEA_init_key(k, in);
    memcpy(buf, out, 8);
    XTEA_encipher_block(buf, k);
    for(i = 0; i < 8; i++)
      out[i] ^= buf[i];
    in += 16;
  }
}

/******************************************************************************/

void ECIES_generate_key_pair(void)      /* generate a public/private key pair */
{
  char buf[8 * NUMWORDS + 1], *bufptr = buf + NUMWORDS * 8 - (DEGREE + 3) / 4;
  elem_t x, y;
  exp_t k;
  get_random_exponent(k);
  point_copy(x, y, base_x, base_y);
  point_mult(x, y, k);
/*
  uart0Puts("Here is your new public/private key pair:\n");
  bitstr_to_hex(buf, x); uart0Puts("Public key: "); uart0Puts(bufptr); uart0Putch(':');
  bitstr_to_hex(buf, y); uart0Puts(bufptr);
  bitstr_to_hex(buf, k); uart0Puts("\nPrivate key: "); uart0Puts(bufptr); uart0Putch('\n');
*/
}

       /* check that a given elem_t-pair is a valid point on the curve != 'o' */
int ECIES_embedded_public_key_validation(const elem_t Px, const elem_t Py)
{
  return (bitstr_sizeinbits(Px) > DEGREE) || (bitstr_sizeinbits(Py) > DEGREE) ||
    point_is_zero(Px, Py) || ! is_point_on_curve(Px, Py) ? -1 : 1;
}

      /* same thing, but check also that (Px,Py) generates a group of order n */
int ECIES_public_key_validation(const char *Px, const char *Py)
{
  elem_t x, y;
  if ((bitstr_parse(x, Px) < 0) || (bitstr_parse(y, Py) < 0))
    return -1;
  if (ECIES_embedded_public_key_validation(x, y) < 0)
    return -1;
  point_mult(x, y, base_order);
  return point_is_zero(x, y) ? 1 : -1;
}

void ECIES_kdf(char *k1, char *k2, const elem_t Zx,     /* a non-standard KDF */
	       const elem_t Rx, const elem_t Ry)
{
  int bufsize = (3 * (4 * NUMWORDS) + 1 + 15) & ~15;
  char buf[bufsize];
  memset(buf, 0, bufsize);
  bitstr_export(buf, Zx);
  bitstr_export(buf + 4 * NUMWORDS, Rx);
  bitstr_export(buf + 8 * NUMWORDS, Ry);
  buf[12 * NUMWORDS] = 0; XTEA_davies_meyer(k1, buf, bufsize / 16);
  buf[12 * NUMWORDS] = 1; XTEA_davies_meyer(k1 + 8, buf, bufsize / 16);
  buf[12 * NUMWORDS] = 2; XTEA_davies_meyer(k2, buf, bufsize / 16);
  buf[12 * NUMWORDS] = 3; XTEA_davies_meyer(k2 + 8, buf, bufsize / 16);
}

#define ECIES_OVERHEAD (8 * NUMWORDS + 8)

                  /* ECIES encryption; the resulting cipher text message will be
                                            (len + ECIES_OVERHEAD) bytes long */
void ECIES_encryption(char *msg, const char *text, int len, 
		      const char *Px, const char *Py)
{
  elem_t Rx, Ry, Zx, Zy;
  char k1[16], k2[16];
  exp_t k;
//memset(msg,0,len+ECIES_OVERHEAD); //in the case buffer was not clean
  do {
    get_random_exponent(k);
    bitstr_parse(Zx, Px);
    bitstr_parse(Zy, Py);
    point_mult(Zx, Zy, k);
    point_double(Zx, Zy);                           /* cofactor h = 2 on B163 */
  } while(point_is_zero(Zx, Zy));
  point_copy(Rx, Ry, base_x, base_y);
  point_mult(Rx, Ry, k);
  ECIES_kdf(k1, k2, Zx, Rx, Ry);
  bitstr_export(msg, Rx);
  bitstr_export(msg + 4 * NUMWORDS, Ry);
  memcpy(msg + 8 * NUMWORDS, text, len);
  XTEA_ctr_crypt(msg + 8 * NUMWORDS, len, k1);
  XTEA_cbcmac(msg + 8 * NUMWORDS + len, msg + 8 * NUMWORDS, len, k2);
}

                                                          /* ECIES decryption */
int ECIES_decryption(char *text, const char *msg, int len, 
		     const char *privkey)
{
  elem_t Rx, Ry, Zx, Zy;
  char k1[16], k2[16], mac[8];
  exp_t d;
  bitstr_import(Rx, msg);
  bitstr_import(Ry, msg + 4 * NUMWORDS);
  if (ECIES_embedded_public_key_validation(Rx, Ry) < 0)
    return -1;
  bitstr_parse(d, privkey);
  point_copy(Zx, Zy, Rx, Ry);
  point_mult(Zx, Zy, d);
  point_double(Zx, Zy);                             /* cofactor h = 2 on B163 */
  if (point_is_zero(Zx, Zy))
    return -1;
  ECIES_kdf(k1, k2, Zx, Rx, Ry);
  
  XTEA_cbcmac(mac, msg + 8 * NUMWORDS, len, k2);
  if (memcmp(mac, msg + 8 * NUMWORDS + len, 8))
    return -1;
  memcpy(text, msg + 8 * NUMWORDS, len);
  XTEA_ctr_crypt(text, len, k1);
  return 1;
}

/******************************************************************************/
/******************************************************************************/


void encryption_decryption_demo(const char *text, const char *public_x,
				const char *public_y, const char *private)
{
  int len = strlen(text) + 1;

  //this used to be a malloc: made the buffer static for a maximum message
  //size of 100

  char *encrypted[100 + ECIES_OVERHEAD];
  char *decrypted[100];

  //uart0Puts("plain text: "); uart0Puts(text);  uart0Putch('\n');
  ECIES_encryption(encrypted, text, len, public_x, public_y);   /* encryption */
  /*uart0Puts("encrypted: "); 
  {int i;
  for(i=0; i<len + ECIES_OVERHEAD; ++i)
		{
		char tmp[9];
		siprintf(tmp,"%02x ",(unsigned char)encrypted[i]);
 		uart0Puts(tmp);
		}
  }
  uart0Putch('\n');
*/

  if (ECIES_decryption(decrypted, encrypted, len, private) < 0) /* decryption */
    {//uart0Puts("decryption failed! :"); uart0Puts(decrypted); uart0Putch('\n');
  DoString(0,32,"dec fail");
}
  else
  {
    //uart0Puts("after encryption/decryption: "); uart0Puts(decrypted); uart0Putch('\n');
  DoString(0,32,decrypted);
 }
  DoString(0,40,"foo");
}

int8_t endianity(void)
{
int8_t jj= -1;
union {uint32_t i; char z[sizeof(uint32_t)];} u;
u.i=1;
jj=0; while(!u.z[jj]) jj++;
return jj;
}

