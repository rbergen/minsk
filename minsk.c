/*
 *	Minsk-2 Emulator
 *
 *	(c) 2010 Martin Mares <mj@ucw.cz>
 */

/*
 * Things that are not implemented:
 *
 *	- rounding modes
 *	- exact behavior of accumulator/R1/R2 (the manual lacks details)
 *	- exact behavior of negative zero
 *	- I/O instructions for devices that are not emulated (paper tape
 *	  reader and puncher, card reader and puncher, magnetic tape unit)
 */

#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#define NORETURN __attribute__((noreturn))

#undef ENABLE_DAEMON_MODE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <assert.h>
#include <math.h>
#include <getopt.h>

static int trace;
static int cpu_quota = -1;
static int print_quota = -1;
static int english;
static int memblocks = 1;
static void (*error_hook)(char *msg);

// Minsk-2 has 37-bit words in sign-magnitude representation (bit 36 = sign)
typedef unsigned long long int word;

#define  MEM_SIZE 4096
#define WORD_MASK 01777777777777ULL
#define SIGN_MASK 01000000000000ULL
#define  VAL_MASK 00777777777777ULL

typedef struct loc
{
  int block;
  int address;
} loc;

static int wsign(word w)
{
  return (w & SIGN_MASK) ? -1 : 1;
}

static word wabs(word w)
{
  return w & VAL_MASK;
}

#define WF(w) (wsign(w) < 0 ? '-' : '+'), wabs(w)
#define LF(a) (a.block), (a.address)

static long long wtoll(word w)
{
  if (wsign(w) < 0)
    return -wabs(w);
  else
    return wabs(w);
}

static word wfromll(long long x)
{
  word w = ((x < 0) ? -x : x) & VAL_MASK;
  if (x < 0)
    w |= SIGN_MASK;
  return w;
}

static double wtofrac(word w)
{
  return (double)wtoll(w) / (double)(1ULL << 36);
}

static word wfromfrac(double d)
{
  return wfromll((long long)(d * (double)(1ULL << 36)));
}

static int int_in_range(long long x)
{
  return (x >= -(long long)VAL_MASK && x <= (long long)VAL_MASK);
}

static int frac_in_range(double d)
{
  return (d > -1. && d < 1.);
}

static int wexp(word w)
{
  int exp = w & 077;
  return (w & 0100 ? -exp : exp);
}

static word wputexp(word w, int exp)
{
  return ((w & ~(word)0177) | ((exp < 0) ? 0100 | (-exp) : exp));
}

static int wmanti(word w)
{
  return ((w >> 8) & ((1 << 28) - 1));
}

static double wtofloat(word w)
{
  double x = wmanti(w);
  return ldexp(x, wexp(w) - 28);
}

static int float_in_range(double x)
{
  x = fabs(x);
  return (x <= ldexp((1 << 28) - 1, 63 - 28));
}

static word wfromfloat(double x, int normalized)
{
  word w = 0;
  if (x < 0)
    {
      w |= SIGN_MASK;
      x = -x;
    }
  int exp;
  double m = frexp(x, &exp);
  word mm = (word) ldexp(m, 28);
  if (exp > 63)
    assert(0);
  else if (exp < -63)
    {
      if (normalized || exp < -91)
	mm=0, exp=0;
      else
	{
	  mm >>= -exp - 63;
	  exp = -63;
	}
    }
  w |= mm << 8;
  if (exp < 0)
    {
      w |= 0100;
      exp = -exp;
    }
  w |= exp;
  return w;
}

static word **mem;

static word rd(loc addr)
{
  word val = addr.address ? mem[addr.block][addr.address] : 0;
  if (trace > 2)
    printf("\tRD %d:%04o = %c%012llo\n", LF(addr), WF(val));
  return val;
}

static void wr(loc addr, word val)
{
  assert(!(val & ~(WORD_MASK)));
  if (trace > 2)
    printf("\tWR %d:%04o = %c%012llo\n", LF(addr), WF(val));
  mem[addr.block][addr.address] = val;
}

static int lino;

NORETURN static void parse_error(char *russian_msg, char *english_msg)
{
  if (error_hook)
    error_hook("Parse error");

  if (english)
    printf("Parse error (line %d): %s\n", lino, english_msg);
  else
    printf("Ошибка входа (стр. %d): %s\n", lino, russian_msg);
  exit(0);
}

static void parse_in(void)
{
  char line[80];
  loc addr = { 0, 0 };

  while (fgets(line, sizeof(line), stdin))
    {
      lino++;
      char *eol = strchr(line, '\n');
      if (!eol)
	parse_error("Строка слишком долгая", "Line too long");
      *eol = 0;
      if (eol > line && eol[-1] == '\r')
	*--eol = 0;

      char *c = line;
      if (!c[0] || c[0] == ';')
	continue;

      if (c[0] == '.')
	return;

      if (c[0] == '@')
	{
	  c++;
	  addr.address = 0;
	  for (int i=0; i<4; i++)
	    {
	      while (*c == ' ')
		c++;
	      if (*c >= '0' && *c <= '7')
		addr.address = 8*addr.address + *c++ - '0';
	      else
		parse_error("Плохая цифра", "Invalid number");
	    }
	  while (*c == ' ')
	    c++;
	  if (*c)
	    parse_error("Адрес слишком долгий", "Address too long");
	  continue;
	}

      word w = 0;
      if (*c == '-')
	w = 1;
      else if (*c != '+')
	parse_error("Плохой знак", "Invalid sign");
      c++;
      for (int i=0; i<12; i++)
	{
	  while (*c == ' ')
	    c++;
	  if (*c >= '0' && *c <= '7')
	    w = 8*w + *c++ - '0';
	  else
	    parse_error("Плохая цифра", "Invalid number");
	}
      while (*c == ' ')
	c++;
      if (*c)
	parse_error("Номер слишком долгий", "Number too long");
      wr(addr, w);
      addr.address = (addr.address+1) & 07777;
    }
}

static word acc;
static word r1, r2, current_ins;
static int ip = 00050;			// Standard program start location
static int prev_ip;

NORETURN static void stop(char *russian_reason, char *english_reason)
{
  if (error_hook)
    error_hook(english_reason);

  if (english)
    {
      printf("System stopped -- %s\n", english_reason);
      printf("IP:%04o ACC:%c%012llo R1:%c%012llo R2:%c%012llo\n", prev_ip, WF(acc), WF(r1), WF(r2));
    }
  else
    {
      printf("Машина остановлена -- %s\n", russian_reason);
      printf("СчАК:%04o См:%c%012llo Р1:%c%012llo Р2:%c%012llo\n", prev_ip, WF(acc), WF(r1), WF(r2));
    }
  exit(0);
}

NORETURN static void over(void)
{
  stop("Аварийный останов", "Overflow");
}

NORETURN static void notimp(void)
{
  acc = current_ins;
  stop("Устройство разбитое", "Not implemented");
}

NORETURN static void noins(void)
{
  acc = current_ins;
  stop("Эту команду не знаю", "Illegal instruction");
}

static uint16_t linebuf[128];

static uint16_t russian_chars[64] = {
	'0',	'1',	'2',	'3',	'4',	'5',	'6',	'7',	// 0x
	'8',	'9',	'+',	'-',	'/',	',',	'.',	' ',	// 1x
	0x2169,	'^',	'(',	')',	0x00d7,	'=',	';',	'[',	// 2x
	']',	'*',	'`',	'\'',	0x2260,	'<',	'>',	':',	// 3x
	0x410,	0x411,	0x412,	0x413,	0x414,	0x415,	0x416,	0x417,	// 4x
	0x418,	0x419,	0x41a,	0x41b,	0x41c,	0x41d,	0x41e,	0x41f,	// 5x
	0x420,	0x421,	0x422,	0x423,	0x424,	0x425,	0x426,	0x427,	// 6x
	0x428,	0x429,	0x42b,	0x42c,	0x42d,	0x42e,	0x42f,	0x2013	// 7x
};

static uint16_t latin_chars[64] = {
	'0',	'1',	'2',	'3',	'4',	'5',	'6',	'7',	// 0x
	'8',	'9',	'+',	'-',	'/',	',',	'.',	' ',	// 1x
	0x2169,	'^',	'(',	')',	0x00d7,	'=',	';',	'[',	// 2x
	']',	'*',	'`',	'\'',	0x2260,	'<',	'>',	':',	// 3x
	'A',	'B',	'W',	'G',	'D',	'E',	'V',	'Z',	// 4x
	'I',	'J',	'K',	'L',	'M',	'N',	'O',	'P',	// 5x
	'R',	'S',	'T',	'U',	'F',	'H',	'C',	' ',	// 6x
	' ',	' ',	'Y',	'X',	' ',	' ',	'Q',	0x2013	// 7x
};

static void print_line(int r)
{
  /*
   *  Meaning of bits of r:
   *	0 = perform line feed
   *	1 = clear buffer
   *	2 = actually print
   */
  if (r & 4)
    {
      if (print_quota > 0 && !--print_quota)
	stop("Бумага дошла - нужно ехать в Сибирь про новую", "Out of paper");
      for (int i=0; i<128; i++)
	{
	  int ch = linebuf[i];
	  if (!ch)
	    ch = ' ';
	  if (ch < 0x80)
	    putchar(ch);
	  else if (ch < 0x800)
	    {
	      putchar(0xc0 | (ch >> 6));
	      putchar(0x80 | (ch & 0x3f));
	    }
	  else
	    {
	      putchar(0xe0 | (ch >> 12));
	      putchar(0x80 | ((ch >> 6) & 0x3f));
	      putchar(0x80 | (ch & 0x3f));
	    }
	}
    }
  if (r & 2)
    memset(linebuf, 0, sizeof(linebuf));
  if (r & 1)
    putchar('\n');
  else if (r & 4)
    putchar('\r');
  fflush(stdout);
}

static void print_ins(int x, loc y)
{
  word yy = rd(y);
  int pos = x & 0177;
  int r = (x >> 9) & 7;

  if (x & 0400)
    {
      print_line(r);
      return;
    }

  char *fmt;
  int bit = 37;
  int eat = 0;
  switch (r)
    {
    case 0:				// Decimal float
      fmt = "+dddddddx+xbd";
      break;
    case 1:				// Octal number
      fmt = "+oooooooooooo";
      break;
    case 2:				// Decimal fixed
      fmt = "+ddddddddd";
      break;
    case 3:				// Decimal unsigned
      fmt = "x ddddddddd";
      eat = 1;
      break;
    case 4:				// One Russian symbol
      fmt = "xr";
      break;
    case 5:				// Russian text
      fmt = "xrrrrrr";
      break;
    case 6:				// One Latin symbol
      fmt = "xl";
      break;
    default:				// Latin text
      fmt = "xllllll";
    }

  while (*fmt)
    {
      int ch;
      switch (*fmt++)
	{
	case 'x':
	  bit--;
	  continue;
	case ' ':
	  ch = ' ';
	  break;
	case '+':
	  bit--;
	  ch = (yy & (1ULL << bit)) ? '-' : '+';
	  break;
	case 'b':
	  bit--;
	  ch = '0' + ((yy >> bit) & 1);
	  break;
	case 'o':
	  bit -= 3;
	  ch = '0' + ((yy >> bit) & 7);
	  break;
	case 'd':
	  bit -= 4;
	  ch = '0' + ((yy >> bit) & 15);
	  if (ch > '0' + 9)
	    ch += 7;
	  break;
	case 'r':
	  bit -= 6;
	  ch = russian_chars[(yy >> bit) & 077];
	  break;
	case 'l':
	  bit -= 6;
	  ch = latin_chars[(yy >> bit) & 077];
	  break;
	default:
	  assert(0);
	}

      if (eat && *fmt)
	{
	  if (ch == '0' || ch == ' ')
	    ch = ' ';
	  else
	    eat = 0;
	}
      linebuf[pos] = ch;
      pos = (pos+1) & 0177;
    }
  assert(bit >= 0);
}

static void run(void)
{
  for (;;)
    {
      r2 = acc;
      prev_ip = ip;
      word w = mem[0][ip];
      current_ins = w;

      int op = (w >> 30) & 0177;	// Operation code
      int ax = (w >> 28) & 3;		// Address extensions supported in Minsk-22 mode
      int ix = (w >> 24) & 15;		// Indexing
      loc x = { ax >> 1, (w >> 12) & 07777 };	// Operands (original form)
      loc y = { ax & 1, w & 07777 };
      loc xi=x, yi=y;			// (indexed form)
      if (trace)
	printf("@%04o  %c%02o %02o %d:%04o %d:%04o\n",
	  ip,
	  (w & SIGN_MASK) ? '-' : '+',
	  (int)((w >> 30) & 077),
	  (int)((w >> 24) & 077),
	  LF(x),
	  LF(y));
      if (ix)
	{
	  if (op != 0120)
	    {
	      loc iaddr = { 0, ix };
	      word i = rd(iaddr);
	      xi.address = (xi.address + (int)((i >> 12) & 07777)) & 07777;
	      yi.address = (yi.address + (int)(i & 07777)) & 07777;
	      if (trace > 2)
		printf("\tIndexing -> %d:%04o %d:%04o\n", LF(xi), LF(yi));
	    }
	}
      ip = (ip+1) & 07777;

      if (cpu_quota > 0 && !--cpu_quota)
	stop("Тайм-аут", "CPU quota exceeded");

      /* Arithmetic operations */

      word a, b, c;
      long long aa, bb, cc;
      double ad, bd;
      int i;

      auto void afetch(void);
      void afetch(void)
	{
	  if (op & 2)
	    a = r2;
	  else
	    a = rd(yi);
	  b = r1 = rd(xi);
	}

      auto void astore(word result);
      void astore(word result)
	{
	  acc = result;
	  if (op & 1)
	    wr(yi, acc);
	}

      auto void astore_int(long long x);
      void astore_int(long long x)
	{
	  if (!int_in_range(x))
	    over();
	  astore(wfromll(x));
	}

      auto void astore_frac(double f);
      void astore_frac(double f)
	{
	  if (!frac_in_range(f))
	    over();
	  astore(wfromfrac(f));
	}

      auto void astore_float(double f);
      void astore_float(double f)
	{
	  if (!float_in_range(f))
	    over();
	  astore(wfromfloat(f, 0));
	}

      if (ax && memblocks == 1)	// Reject address extensions if we only have 1 memory block
	op = -1;
      switch (op)
	{
	case 000:		// NOP
	  break;
	case 004 ... 007:	// XOR
	  afetch();
	  astore(a^b);
	  break;
	case 010 ... 013:	// FIX addition
	  afetch();
	  astore_int(wtoll(a) + wtoll(b));
	  break;
	case 014 ... 017:	// FP addition
	  afetch();
	  astore_float(wtofloat(a) + wtofloat(b));
	  break;
	case 020 ... 023:	// FIX subtraction
	  afetch();
	  astore_int(wtoll(a) - wtoll(b));
	  break;
	case 024 ... 027:	// FP subtraction
	  afetch();
	  astore_float(wtofloat(a) - wtofloat(b));
	  break;
	case 030 ... 033:	// FIX multiplication
	  afetch();
	  astore_frac(wtofrac(a) * wtofrac(b));
	  break;
	case 034 ... 037:	// FP multiplication
	  afetch();
	  astore_float(wtofloat(a) * wtofloat(b));
	  break;
	case 040 ... 043:	// FIX division
	  afetch();
	  ad = wtofrac(a);
	  bd = wtofrac(b);
	  if (!wabs(b))
	    over();
	  astore_frac(ad / bd);
	  break;
	case 044 ... 047:	// FP division
	  afetch();
	  ad = wtofloat(a);
	  bd = wtofloat(b);
	  if (!bd || wexp(b) < -63)
	    over();
	  astore_float(ad / bd);
	  break;
	case 050 ... 053:	// FIX subtraction of abs values
	  afetch();
	  astore_int(wabs(a) - wabs(b));
	  break;
	case 054 ... 057:	// FP subtraction of abs values
	  afetch();
	  astore_float(fabs(wtofloat(a)) - fabs(wtofloat(b)));
	  break;
	case 060 ... 063:	// Shift logical
	  afetch();
	  i = wexp(b);
	  if (i <= -37 || i >= 37)
	    astore(0);
	  else if (i >= 0)
	    astore((a << i) & WORD_MASK);
	  else
	    astore(a >> (-i));
	  break;
	case 064 ... 067:	// Shift arithmetical
	  afetch();
	  i = wexp(b);
	  aa = wabs(a);
	  if (i <= -36 || i >= 36)
	    cc = 0;
	  else if (i >= 0)
	    cc = (aa << i) & VAL_MASK;
	  else
	    cc = aa >> (-i);
	  astore((a & SIGN_MASK) | wfromll(cc));
	  break;
	case 070 ... 073:	// And
	  afetch();
	  astore(a&b);
	  break;
	case 074 ... 077:	// Or
	  afetch();
	  astore(a|b);
	  break;

	case 0100:		// Halt
	  r1 = rd(x);
	  acc = rd(y);
	  stop("Останов машины", "Halted");
	case 0103:		// I/O magtape
	  notimp();
	case 0104:		// Disable rounding
	  notimp();
	case 0105:		// Enable rounding
	  notimp();
	case 0106:		// Interrupt control
	  notimp();
	case 0107:		// Reverse tape
	  notimp();
	case 0110:		// Move
	  wr(yi, r1 = acc = rd(xi));
	  break;
	case 0111:		// Move negative
	  wr(yi, acc = (r1 = rd(xi)) ^ SIGN_MASK);
	  break;
	case 0112:		// Move absolute value
	  wr(yi, acc = (r1 = rd(xi)) & VAL_MASK);
	  break;
	case 0113:		// Read from keyboard
	  notimp();
	case 0114:		// Copy sign
	  wr(yi, acc = rd(yi) ^ ((r1 = rd(xi)) & SIGN_MASK));
	  break;
	case 0115:		// Read code from R1 (obscure)
	  notimp();
	case 0116:		// Copy exponent
	  wr(yi, acc = wputexp(rd(yi), wexp(r1 = rd(xi))));
	  break;
	case 0117:		// I/O teletype
	  notimp();
	case 0120:		// Loop
	  if (!ix)
	    noins();
	  loc iaddr = { 0, ix };
	  a = r1 = rd(iaddr);
	  aa = (a >> 24) & 017777;
	  if (!aa)
	    break;
	  b = rd(y);		// (a mountain range near Prague)
	  acc = ((aa-1) << 24) |
		(((((a >> 12) & 07777) + (b >> 12) & 07777) & 07777) << 12) |
		(((a & 07777) + (b & 07777)) & 07777);
	  wr(iaddr, acc);
	  ip = x.address;
	  break;
	case 0130:		// Jump
	  wr(y, r2);
	  ip = x.address;
	  break;
	case 0131:		// Jump to subroutine
	  wr(y, acc = ((0130ULL << 30) | ((ip & 07777ULL) << 12)));
	  ip = x.address;
	  break;
	case 0132:		// Jump if positive
	  if (wsign(r2) >= 0)
	    ip = x.address;
	  else
	    ip = y.address;
	  break;
	case 0133:		// Jump if overflow
	  // Since we always trap on overflow, this instruction always jumps to the 1st address
	  ip = x.address;
	  break;
	case 0134:		// Jump if zero
	  if (!wabs(r2))
	    ip = y.address;
	  else
	    ip = x.address;
	  break;
	case 0135:		// Jump if key pressed
	  // No keys are ever pressed, so always jump to 2nd
	  ip = y.address;
	  break;
	case 0136:		// Interrupt masking
	  notimp();
	case 0137:		// Used only when reading from tape
	  notimp();
	case 0140 ... 0147:	// I/O
	  notimp();
	case 0150 ... 0154:	// I/O
	  notimp();
	case 0160 ... 0161:	// I/O
	  notimp();
	case 0162:		// Printing
	  print_ins(x.address, y);
	  break;
	case 0163:		// I/O
	  notimp();
	case 0170:		// FIX multiplication, bottom part
	  afetch();
	  if (wtofrac(a) * wtofrac(b) >= .1/(1ULL << 32))
	    over();
	  acc = wfromll(((unsigned long long)wabs(a) * (unsigned long long)wabs(b)) & VAL_MASK);
	  // XXX: What should be the sign? The book does not define that.
	  break;
	case 0171:		// Modulo
	  afetch();
	  aa = wabs(a);
	  bb = wabs(b);
	  if (!bb)
	    over();
	  cc = aa % bb;
	  if (wsign(b) < 0)
	    cc = -cc;
	  acc = wfromll(cc);
	  break;
	case 0172:		// Add exponents
	  a = r1 = rd(xi);
	  b = rd(yi);
	  i = wexp(a) + wexp(b);
	  if (i < -63 || i > 63)
	    over();
	  acc = wputexp(b, i);
	  wr(yi, acc);
	  break;
	case 0173:		// Sub exponents
	  a = r1 = rd(xi);
	  b = rd(yi);
	  i = wexp(b) - wexp(a);
	  if (i < -63 || i > 63)
	    over();
	  acc = wputexp(b, i);
	  wr(yi, acc);
	  break;
	case 0174:		// Addition in one's complement
	  a = r1 = rd(xi);
	  b = rd(yi);
	  c = a + b;
	  if (c > VAL_MASK)
	    c = c - VAL_MASK;
	  wr(yi, c);
	  // XXX: The effect on the accumulator is undocumented, but likely to be as follows:
	  acc = c;
	  break;
	case 0175:		// Normalization
	  a = r1 = rd(xi);
	  if (!wabs(a))
	    {
	      wr(yi, 0);
	      loc yinc = { yi.block, (yi.address+1) & 07777 };
	      wr(yinc, 0);
	      acc = 0;
	    }
	  else
	    {
	      i = 0;
	      acc = a & SIGN_MASK;
	      a &= VAL_MASK;
	      while (!(a & (SIGN_MASK >> 1)))
		{
		  a <<= 1;
		  i++;
		}
	      acc |= a;
	      wr(yi, acc);
	      loc yinc = { yi.block, (yi.address+1) & 07777 };
	      wr(yinc, i);
	    }
	  break;
	case 0176:		// Population count
	  a = r1 = rd(xi);
	  cc = 0;
	  for (int i=0; i<36; i++)
	    if (a & (1ULL << i))
	      cc++;
	  // XXX: Guessing that acc gets a copy of the result
	  acc = wfromll(cc);
	  wr(yi, acc);
	  break;
	default:
	  noins();
	}

      if (trace > 1)
	printf("\tACC:%c%012llo R1:%c%012llo R2:%c%012llo\n", WF(acc), WF(r1), WF(r2));
    }
}

NORETURN static void die(char *msg)
{
  fprintf(stderr, "minsk: %s\n", msg);
  exit(1);
}

/*** Daemon interface ***/

#ifdef ENABLE_DAEMON_MODE

/*
 * The daemon mode was a quick hack for the Po drate contest.
 * Most parameters are hard-wired.
 */

#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if 0
#define DTRACE(msg, args...) fprintf(stderr, msg "\n", ##args)
#define DLOG(msg, args...) fprintf(stderr, msg "\n", ##args)
#else
#define DTRACE(msg, args...) do { } while(0)
#define DLOG(msg, args...) syslog(LOG_INFO, msg, ##args)
#endif

#define MAX_CONNECTIONS 50		// Per daemon
#define MAX_CONNS_PER_IP 1		// Per IP
#define MAX_TRACKERS 200		// IP address trackers
#define TBF_MAX 5			// Max number of tokens in the bucket
#define TBF_REFILL_PER_SEC 0.2		// Bucket refill rate (buckets/sec)

#define PID_FILE "/var/run/pd-minsk.pid"
#define UID 124
#define GID 125

static char **spt_argv;
static char *spt_start, *spt_end;

static void setproctitle_init(int argc, char **argv)
{
  int i, len;
  char **env, **oldenv, *t;

  spt_argv = argv;

  /* Create a backup copy of environment */
  oldenv = __environ;
  len = 0;
  for (i=0; oldenv[i]; i++)
    len += strlen(oldenv[i]) + 1;
  __environ = env = malloc(sizeof(char *)*(i+1));
  t = malloc(len);
  if (!__environ || !t)
    die("malloc failed");
  for (i=0; oldenv[i]; i++)
    {
      env[i] = t;
      len = strlen(oldenv[i]) + 1;
      memcpy(t, oldenv[i], len);
      t += len;
    }
  env[i] = NULL;

  /* Scan for consecutive free space */
  spt_start = spt_end = argv[0];
  for (i=0; i<argc; i++)
    if (!i || spt_end+1 == argv[i])
      spt_end = argv[i] + strlen(argv[i]);
  for (i=0; oldenv[i]; i++)
    if (spt_end+1 == oldenv[i])
      spt_end = oldenv[i] + strlen(oldenv[i]);
}

static void
setproctitle(const char *msg, ...)
{
  va_list args;
  char buf[256];
  int n;

  va_start(args, msg);
  if (spt_end > spt_start)
    {
      n = vsnprintf(buf, sizeof(buf), msg, args);
      if (n >= (int) sizeof(buf) || n < 0)
	sprintf(buf, "<too-long>");
      n = spt_end - spt_start;
      strncpy(spt_start, buf, n);
      spt_start[n] = 0;
      spt_argv[0] = spt_start;
      spt_argv[1] = NULL;
    }
  va_end(args);
}

static void sigchld_handler(int sig UNUSED)
{
}

static void sigalrm_handler(int sig UNUSED)
{
  const char err[] = "--- Timed out. Time machine disconnected. ---\n";
  write(1, err, sizeof(err));
  DLOG("Connection timed out");
  exit(0);
}

static void child_error_hook(char *err)
{
  DLOG("Stopped: %s", err);
}

static void child(int sk2)
{
  dup2(sk2, 0);
  dup2(sk2, 1);
  close(sk2);

  struct sigaction sact = {
    .sa_handler = sigalrm_handler,
  };
  if (sigaction(SIGALRM, &sact, NULL) < 0)
    die("sigaction: %m");

  // Set up limits
  alarm(60);
  cpu_quota = 100000;
  print_quota = 100;

  const char welcome[] = "+++ Welcome to our computer museum. +++\n+++ Our time machine will connect you to one of our exhibits. +++\n\n";
  write(1, welcome, sizeof(welcome));

  error_hook = child_error_hook;
  parse_in();
  run();
  fflush(stdout);
  DTRACE("Finished");
}

struct conn {
  pid_t pid;
  struct in_addr addr;
  struct tracker *tracker;
};

static struct conn connections[MAX_CONNECTIONS];

static struct conn *get_conn(struct in_addr *a)
{
  for (int i=0; i<MAX_CONNECTIONS; i++)
    {
      struct conn *c = &connections[i];
      if (!c->pid)
	{
	  memcpy(&c->addr, a, sizeof(struct in_addr));
	  return c;
	}
    }
  return NULL;
}

static struct conn *pid_to_conn(pid_t pid)
{
  for (int i=0; i<MAX_CONNECTIONS; i++)
    {
      struct conn *c = &connections[i];
      if (c->pid == pid)
	return c;
    }
  return NULL;
}

static void put_conn(struct conn *c)
{
  c->pid = 0;
  c->tracker = NULL;
}

struct tracker {
  struct in_addr addr;
  int active_conns;
  time_t last_access;
  double tokens;
};

static struct tracker trackers[MAX_TRACKERS];

static int get_tracker(struct conn *c)
{
  struct tracker *t;
  time_t now = time(NULL);
  int i;

  for (i=0; i<MAX_TRACKERS; i++)
    {
      t = &trackers[i];
      if (!memcmp(&t->addr, &c->addr, sizeof(struct in_addr)))
	break;
    }
  if (i < MAX_TRACKERS)
    {
      if (now > t->last_access)
	{
	  t->tokens += (now - t->last_access) * (double) TBF_REFILL_PER_SEC;
	  t->last_access = now;
	  if (t->tokens > TBF_MAX)
	    t->tokens = TBF_MAX;
	}
      DTRACE("TBF: Using tracker %d (%.3f tokens)", i, t->tokens);
    }
  else
    {
      int min_i = -1;
      for (int i=0; i<MAX_TRACKERS; i++)
	{
	  t = &trackers[i];
	  if (!t->active_conns && (min_i < 0 || t->last_access < trackers[min_i].last_access))
	    min_i = i;
	}
      if (min_i < 0)
	{
	  DLOG("TBF: Out of trackers!");
	  return 0;
	}
      t = &trackers[min_i];
      if (t->last_access)
	DTRACE("TBF: Recycling tracker %d", min_i);
      else
	DTRACE("TBF: Creating tracker %d", min_i);
      memset(t, 0, sizeof(*t));
      t->addr = c->addr;
      t->last_access = now;
      t->tokens = TBF_MAX;
    }

  if (t->active_conns >= MAX_CONNS_PER_IP)
    {
      DTRACE("TBF: Too many conns per IP");
      return 0;
    }

  if (t->tokens >= 0.999)
    {
      t->tokens -= 1;
      t->active_conns++;
      c->tracker = t;
      DTRACE("TBF: Passed (%d conns)", t->active_conns);
      return 1;
    }
  else
    {
      DTRACE("TBF: Failed");
      t->tokens = 0;
      return 0;
    }
}

static void put_tracker(struct conn *c)
{
  struct tracker *t = c->tracker;
  if (!t)
    {
      DLOG("put_tracker: no tracker?");
      sleep(5);
      return;
    }
  if (t->active_conns <= 0)
    {
      DLOG("put_tracker: no counter?");
      sleep(5);
      return;
    }
  t->active_conns--;
  DTRACE("TBF: Put tracker (%d conns remain)", t->active_conns);
}

static void run_as_daemon(int do_fork)
{
  int sk = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sk < 0)
    die("socket: %m");

  int one = 1;
  if (setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
    die("setsockopt: %m");

  struct sockaddr_in sa = {
    .sin_family = AF_INET,
    .sin_port = ntohs(1969),
    .sin_addr.s_addr = INADDR_ANY,
  };
  if (bind(sk, (struct sockaddr *) &sa, sizeof(sa)) < 0)
    die("bind: %m");
  if (listen(sk, 128) < 0)
    die("listen: %m");
  // if (fcntl(sk, F_SETFL, O_NONBLOCK) < 0)
  //  die("fcntl: %m");

  if (do_fork)
    {
      pid_t pid = fork();
      if (pid < 0)
	die("fork: %m");
      if (pid)
	{
	  FILE *f = fopen(PID_FILE, "w");
	  if (f)
	    {
	      fprintf(f, "%d\n", pid);
	      fclose(f);
	    }
	  exit(0);
	}

      chdir("/");
      setresgid(GID, GID, GID);
      setresuid(UID, UID, UID);
      setsid();
    }

  struct sigaction sact = {
    .sa_handler = sigchld_handler,
    .sa_flags = SA_RESTART,
  };
  if (sigaction(SIGCHLD, &sact, NULL) < 0)
    die("sigaction: %m");

  DLOG("Daemon ready");
  setproctitle("minsk: Listening");
  openlog("minsk", LOG_PID, LOG_LOCAL7);

  for (;;)
    {
      struct pollfd pfd[1] = {
	{ .fd = sk, .events = POLLIN },
      };

      int nfds = poll(pfd, 1, 60000);
      if (nfds < 0 && errno != EINTR)
	{
	  DLOG("poll: %m");
	  sleep(5);
	  continue;
	}

      int status;
      pid_t pid;
      while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
	  if (!WIFEXITED(status) || WEXITSTATUS(status))
	    DLOG("Process %d exited with strange status %x", pid, status);

	  struct conn *conn = pid_to_conn(pid);
	  if (conn)
	    {
	      DTRACE("Connection with PID %d exited", pid);
	      put_tracker(conn);
	      put_conn(conn);
	    }
	  else
	    DTRACE("PID %d exited, matching no connection", pid);
	}

      if (!(pfd[0].revents & POLLIN))
	continue;

      socklen_t salen = sizeof(sa);
      int sk2 = accept(sk, (struct sockaddr *) &sa, &salen);
      if (sk2 < 0)
	{
	  if (errno != EINTR)
	    {
	      DLOG("accept: %m");
	      sleep(5);
	    }
	  continue;
	}
      DTRACE("Got connection: fd=%d", sk2);

      struct conn *conn = get_conn(&sa.sin_addr);
      const char *reason = NULL;
      if (conn)
	{
	  if (!get_tracker(conn))
	    {
	      DLOG("Connection from %s dropped: Throttling", inet_ntoa(sa.sin_addr));
	      put_conn(conn);
	      conn = NULL;
	      reason = "--- Sorry, but you are sending too many requests. Please slow down. ---\n";
	    }
	}
      else
	{
	  DLOG("Connection from %s dropped: Too many connections", inet_ntoa(sa.sin_addr));
	  reason = "--- Sorry, maximum number of connections exceeded. Please come later. ---\n";
	}

      pid = fork();
      if (pid < 0)
	{
	  DLOG("fork failed: %m");
	  close(sk2);
	  continue;
	}
      if (!pid)
	{
	  close(sk);
	  if (conn)
	    {
	      DLOG("Accepted connection from %s", inet_ntoa(sa.sin_addr));
	      setproctitle("minsk: %s", inet_ntoa(sa.sin_addr));
	      child(sk2);
	    }
	  else
	    {
	      DLOG("Sending error message to %s", inet_ntoa(sa.sin_addr));
	      setproctitle("minsk: %s ERR", inet_ntoa(sa.sin_addr));
	      write(sk2, reason, strlen(reason));
	    }
	  exit(0);
	}

      DTRACE("Created process %d", pid);
      if (conn)
	conn->pid = pid;
      close(sk2);
    }
}

#else

static void run_as_daemon(int do_fork UNUSED)
{
  die("Daemon mode not supported in this version, need to recompile.");
}

static void setproctitle_init(int argc UNUSED, char **argv UNUSED)
{
}

#endif

static void init_memory(int set_password)
{
  mem = malloc(memblocks * sizeof(word *));
  for (int i=0; i<memblocks; i++)
    mem[i] = malloc(MEM_SIZE * sizeof(word));

  if (set_password)
    {
      // For the contest, we fill the whole memory with -00 00 0000 0000 (HALT),
      // not +00 00 0000 0000 (NOP). Otherwise, an empty program would reveal
      // the location of the password :)
      for (int i=0; i<memblocks; i++)
        for (int j=0; j<MEM_SIZE; j++)
          mem[i][j] = 01000000000000ULL;

      // Store the password
      int pos = 02655;
      mem[0][pos++] = 0574060565373;
      mem[0][pos++] = 0371741405340;
      mem[0][pos++] = 0534051524017;
    }
  else
    for (int i=0; i<memblocks; i++)
      for (int j=0; j<MEM_SIZE; j++)
        mem[i][j] = 00000000000000ULL;
}

static const struct option longopts[] = {
  { "cpu-quota",	required_argument, 	NULL, 'q' },
  { "daemon",		no_argument, 		NULL, 'd' },
  { "nofork",		no_argument, 		NULL, 'n' },
  { "english",		no_argument,		NULL, 'e' },
  { "set-password",	no_argument,		NULL, 's' },
  { "upgrade",		no_argument,		NULL, 'u' },
  { "print-quota",	required_argument, 	NULL, 'p' },
  { "trace",		required_argument, 	NULL, 't' },
  { NULL,		0, 			NULL, 0   },
};

static void usage(void)
{
  fprintf(stderr, "Options:\n\n");
  #ifdef ENABLE_DAEMON_MODE
  fprintf(stderr, "\
-d, --daemon		Run as daemon and listen for network connections\n\
-n, --nofork		When run with --daemon, avoid forking\n\
");
  #endif
  fprintf(stderr, "\
-e, --english		Print messages in English\n\
-s, --set-password	Put hidden password in memory\n\
-u, --upgrade		Upgrade the Minsk-2 to the Minsk-22\n\
-t, --trace=<level>	Enable tracing of program execution\n\
-q, --cpu-quota=<n>	Set CPU quota to <n> instructions\n\
-p, --print-quota=<n>	Set printer quota to <n> lines\n\
");
  exit(1);
}

int main(int argc, char **argv)
{
  int opt;
  int daemon_mode = 0;
  int do_fork = 1;
  int set_password = 0;

  while ((opt = getopt_long(argc, argv, "q:desunp:t:", longopts, NULL)) >= 0)
    switch (opt)
      {
      case 'd':
	daemon_mode = 1;
	break;
      case 'n':
	do_fork = 0;
	break;
      case 'e':
	english = 1;
	break;
      case 's':
	set_password = 1;
	break;
      case 'u':
	memblocks = 2;
	break;
      case 'p':
	print_quota = atoi(optarg);
	break;
      case 'q':
	cpu_quota = atoi(optarg);
	break;
      case 't':
	trace = atoi(optarg);
	break;
      default:
	usage();
      }
  if (optind < argc)
    usage();

  setproctitle_init(argc, argv);
  init_memory(set_password);

  if (daemon_mode)
    run_as_daemon(do_fork);

  parse_in();
  run();

  return 0;
}
