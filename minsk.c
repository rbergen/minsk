/*
 *	Minsk-2 Emulator
 *
 *	(c) 2010 Martin Mares <mj@ucw.cz>
 */

/*
 * TODO:
 *	- error messages
 *	- debugging/play mode
 *	- we probably have to disable NOP
 */

/*
 * Things that are not implemented:
 *	- rounding modes
 *	- exact behavior of accumulator/R1/R2 (the manual lacks details)
 *	- exact behavior of negative zero
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <math.h>

static int trace = 3;
static int cpu_quota = -1;
static int print_quota = -1;

// Minsk-2 has 37-bit words in sign-magnitude representation (bit 36 = sign)
typedef unsigned long long int word;

#define WORD_MASK 01777777777777ULL
#define SIGN_MASK 01000000000000ULL
#define  VAL_MASK 00777777777777ULL

static int wsign(word w)
{
  return (w & SIGN_MASK) ? -1 : 1;
}

static word wabs(word w)
{
  return w & VAL_MASK;
}

#define WF(w) (wsign(w) < 0 ? '-' : '+'), wabs(w)

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

static word mem[4096];

static word rd(int addr)
{
  word val = addr ? mem[addr] : 0;
  if (trace > 2)
    printf("\tRD %04o = %c%012llo\n", addr, WF(val));
  return val;
}

static void wr(int addr, word val)
{
  assert(!(val & ~(WORD_MASK)));
  if (trace > 2)
    printf("\tWR %04o = %c%012llo\n", addr, WF(val));
  mem[addr] = val;
}

static int lino;

static void parse_error(char *msg)
{
  printf("Ошибка входа (стр. %d): %s\n", lino, msg);
  exit(1);
}

static void parse_in(void)
{
  char line[80];
  int addr = 0;

  while (fgets(line, sizeof(line), stdin))
    {
      lino++;
      char *eol = strchr(line, '\n');
      if (!eol)
	parse_error("Строка слишком долгая");
      *eol = 0;

      char *c = line;
      if (!c[0] || c[0] == ';')
	continue;
      if (c[0] == '@')
	{
	  c++;
	  addr = 0;
	  for (int i=0; i<4; i++)
	    {
	      while (*c == ' ')
		c++;
	      if (*c >= '0' && *c <= '7')
		addr = 8*addr + *c++ - '0';
	      else
		parse_error("Плохая цифва");
	    }
	  while (*c == ' ')
	    c++;
	  if (*c)
	    parse_error("Адрес слишком долгий");
	  continue;
	}

      word w = 0;
      if (*c == '-')
	w = 1;
      else if (*c != '+')
	parse_error("Плохой знак");
      c++;
      for (int i=0; i<12; i++)
	{
	  while (*c == ' ')
	    c++;
	  if (*c >= '0' && *c <= '7')
	    w = 8*w + *c++ - '0';
	  else
	    parse_error("Плохая цифва");
	}
      while (*c == ' ')
	c++;
      if (*c)
	parse_error("Номер слишком долгий");
      wr(addr++, w);
      addr &= 07777;
    }
}

static word acc;
static word r1, r2, current_ins;
static int ip = 00050;			// Standard program start location
static int prev_ip;

static void stop(char *reason)
{
  printf("MACHINE STOPPED -- %s\n", reason);
  printf("IP:%04o ACC:%c%012llo R1:%c%012llo R2:%c%012llo\n", prev_ip, WF(acc), WF(r1), WF(r2));
  exit(0);
}

static void over(void)
{
  stop("OVERFLOW");
}

static void notimp(void)
{
  acc = current_ins;
  stop("NOT IMPLEMENTED");
}

static void noins(void)
{
  acc = current_ins;
  stop("ILLEGAL INSTRUCTION");
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
	stop("OUT OF PAPER");
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

static void print_ins(int x, int y)
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
      bit = 6;
      fmt = "r";
      break;
    case 5:				// Russian text
      fmt = "xrrrrrr";
      break;
    case 6:				// One Latin symbol
      bit = 6;
      fmt = "l";
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
  assert(!bit);
}

static void run(void)
{
  for (;;)
    {
      r2 = acc;
      prev_ip = ip;
      word w = mem[ip];
      current_ins = w;

      int op = (w >> 30) & 0177;	// Operation code
      int ax = (w >> 28) & 3;		// Address extensions not supported
      int ix = (w >> 24) & 15;		// Indexing
      int x = (w >> 12) & 07777;	// Operands (original form)
      int y = w & 07777;
      int xi=x, yi=y;			// (indexed form)
      if (trace)
	printf("@%04o  %c%02o %02o %04o %04o\n",
	  ip,
	  (w & SIGN_MASK) ? '-' : '+',
	  (int)((w >> 30) & 077),
	  (int)((w >> 24) & 077),
	  x,
	  y);
      if (ix)
	{
	  if (op != 0120)
	    {
	      word i = rd(ix);
	      xi = (xi + (int)((i >> 12) & 07777)) & 07777;
	      yi = (yi + (int)(i & 07777)) & 07777;
	      if (trace > 2)
		printf("\tIndexing -> %04o %04o\n", xi, yi);
	    }
	}
      ip = (ip+1) & 07777;

      if (cpu_quota > 0 && !--cpu_quota)
	stop("TIMED OUT");

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

      if (ax)
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
	    stop("DIVISION BY ZERO");
	  astore_frac(ad / bd);
	  break;
	case 044 ... 047:	// FP division
	  afetch();
	  ad = wtofloat(a);
	  bd = wtofloat(b);
	  if (!bd)
	    stop("DIVISION BY ZERO");
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
	  stop("HALTED");
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
	  a = r1 = rd(ix);
	  aa = (a >> 24) & 017777;
	  if (!aa)
	    break;
	  b = rd(y);		// (a mountain range near Prague)
	  acc = ((aa-1) << 24) |
		(((((a >> 12) & 07777) + (b >> 12) & 07777) & 07777) << 12) |
		(((a & 07777) + (b & 07777)) & 07777);
	  wr(ix, acc);
	  ip = x;
	  break;
	case 0130:		// Jump
	  wr(y, r2);
	  ip = x;
	  break;
	case 0131:		// Jump to subroutine
	  wr(y, acc = ((030ULL << 30) | ((ip & 07777ULL) << 12)));
	  ip = x;
	  break;
	case 0132:		// Jump if positive
	  if (wsign(r2) >= 0)
	    ip = x;
	  else
	    ip = y;
	  break;
	case 0133:		// Jump if overflow
	  // Since we always trap on overflow, this instruction always jumps to the 1st address
	  ip = x;
	  break;
	case 0134:		// Jump if zero
	  if (!wabs(r2))
	    ip = y;
	  else
	    ip = x;
	  break;
	case 0135:		// Jump if key pressed
	  // No keys are ever pressed, so always jump to 2nd
	  ip = y;
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
	  print_ins(x, y);
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
	    stop("DIVISION BY ZERO");
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
	      wr((yi+1) & 07777, 0);
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
	      wr((yi+1) & 07777, i);
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

int main(void)
{
  parse_in();
  run();
  return 0;
}
