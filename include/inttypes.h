#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

#if __SIZEOF_LONG__ == 8
#define __PRI64 "l"
#define __PRIMAX "l"
#else
#define __PRI64 "ll"
#define __PRIMAX "ll"
#endif

#if __SIZEOF_POINTER__ == 8
#define __PRIPTR "l"
#else
#define __PRIPTR ""
#endif

#define PRId8 "hhd"
#define PRIi8 "hhi"
#define PRIo8 "hho"
#define PRIu8 "hhu"
#define PRIx8 "hhx"
#define PRIX8 "hhX"

#define PRId16 "hd"
#define PRIi16 "hi"
#define PRIo16 "ho"
#define PRIu16 "hu"
#define PRIx16 "hx"
#define PRIX16 "hX"

#define PRId32 "d"
#define PRIi32 "i"
#define PRIo32 "o"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"

#define PRId64 __PRI64 "d"
#define PRIi64 __PRI64 "i"
#define PRIo64 __PRI64 "o"
#define PRIu64 __PRI64 "u"
#define PRIx64 __PRI64 "x"
#define PRIX64 __PRI64 "X"

#define PRIdLEAST8 PRId8
#define PRIiLEAST8 PRIi8
#define PRIoLEAST8 PRIo8
#define PRIuLEAST8 PRIu8
#define PRIxLEAST8 PRIx8
#define PRIXLEAST8 PRIX8

#define PRIdLEAST16 PRId16
#define PRIiLEAST16 PRIi16
#define PRIoLEAST16 PRIo16
#define PRIuLEAST16 PRIu16
#define PRIxLEAST16 PRIx16
#define PRIXLEAST16 PRIX16

#define PRIdLEAST32 PRId32
#define PRIiLEAST32 PRIi32
#define PRIoLEAST32 PRIo32
#define PRIuLEAST32 PRIu32
#define PRIxLEAST32 PRIx32
#define PRIXLEAST32 PRIX32

#define PRIdLEAST64 PRId64
#define PRIiLEAST64 PRIi64
#define PRIoLEAST64 PRIo64
#define PRIuLEAST64 PRIu64
#define PRIxLEAST64 PRIx64
#define PRIXLEAST64 PRIX64

#define PRIdFAST8 "d"
#define PRIiFAST8 "i"
#define PRIoFAST8 "o"
#define PRIuFAST8 "u"
#define PRIxFAST8 "x"
#define PRIXFAST8 "X"

#define PRIdFAST16 "d"
#define PRIiFAST16 "i"
#define PRIoFAST16 "o"
#define PRIuFAST16 "u"
#define PRIxFAST16 "x"
#define PRIXFAST16 "X"

#define PRIdFAST32 "d"
#define PRIiFAST32 "i"
#define PRIoFAST32 "o"
#define PRIuFAST32 "u"
#define PRIxFAST32 "x"
#define PRIXFAST32 "X"

#define PRIdFAST64 PRId64
#define PRIiFAST64 PRIi64
#define PRIoFAST64 PRIo64
#define PRIuFAST64 PRIu64
#define PRIxFAST64 PRIx64
#define PRIXFAST64 PRIX64

#define PRIdPTR __PRIPTR "d"
#define PRIiPTR __PRIPTR "i"
#define PRIoPTR __PRIPTR "o"
#define PRIuPTR __PRIPTR "u"
#define PRIxPTR __PRIPTR "x"
#define PRIXPTR __PRIPTR "X"

#define PRIdMAX __PRIMAX "d"
#define PRIiMAX __PRIMAX "i"
#define PRIoMAX __PRIMAX "o"
#define PRIuMAX __PRIMAX "u"
#define PRIxMAX __PRIMAX "x"
#define PRIXMAX __PRIMAX "X"

#endif
