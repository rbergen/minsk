/*
 *	Minsk-2 Emulator
 *
 *	(c) 2010 Martin Mares <mj@ucw.cz>
 */

/*
 * TODO:
 *	- time limit
 *	- error messages
 *	- debugging/play mode
 *	- implement printing
 *	- floating point?
 *	- we probably have to disable NOP
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static int trace = 3;

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

static int inrange(long long x)
{
  return (x >= -(long long)VAL_MASK && x <= (long long)VAL_MASK);
}

static int fracinrange(double d)
{
  return (d > -1. && d < 1.);
}

static int wexp(word w)
{
  int exp = w & 077;
  return (w & 0100 ? -exp : exp);
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
static int flag_zero = 0;
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

static void nofpu(void)
{
  acc = current_ins;
  stop("NO FPU");
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

static void print_ins(int x, int y)
{
  word yy = rd(y);
  int pos = x & 0177;
  int r = (x >> 9) & 7;

  if (x & 0400)
    {
      // r bit 0 = line feed
      // r bit 1 = clear buffer
      // r bit 2 = print
      return;
    }

  switch (r)
    {
    case 0:				// Decimal float
    case 1:				// Octal number
    case 2:				// Decimal fixed
    case 3:				// Decimal unsigned
    case 4:				// One Russian symbol
    case 5:				// Russian text
    case 6:				// One Latin symbol
    case 7:				// Latin text
      noins();
    }
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

      /* Arithmetic operations */

      word a, b, c;
      long long aa, bb, cc;
      double ad, bd, cd;
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
	  cc = wtoll(a) + wtoll(b);
	  if (!inrange(cc))
	    over();
	  astore(wfromll(cc));
	  break;
	case 014 ... 017:	// FP addition
	  nofpu();
	case 020 ... 023:	// FIX subtraction
	  afetch();
	  cc = wtoll(a) - wtoll(b);
	  if (!inrange(cc))
	    over();
	  astore(wfromll(cc));
	  break;
	case 024 ... 027:	// FP subtraction
	  nofpu();
	case 030 ... 033:	// FIX multiplication
	  afetch();
	  // XXX: We ignore the rounding mode settings
	  cd = wtofrac(a) * wtofrac(b);
	  astore(wfromfrac(cd));
	  break;
	case 034 ... 037:	// FP multiplication
	  nofpu();
	case 040 ... 043:	// division
	  afetch();
	  ad = wtofrac(a);
	  bd = wtofrac(b);
	  if (!wabs(b))
	    stop("DIVISION BY ZERO");
	  cd = ad / bd;
	  if (!fracinrange(cd))
	    over();
	  astore(wfromfrac(cd));
	  break;
	case 044 ... 047:	// FP division
	  nofpu();
	case 050 ... 053:	// FIX subtraction of abs values
	  afetch();
	  cc = wabs(a) - wabs(b);
	  if (!inrange(cc))
	    over();
	  astore(wfromll(cc));
	  break;
	case 054 ... 057:	// FP subtraction of abs values
	  nofpu();
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
	  nofpu();
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
	  if (flag_zero)
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
	  nofpu();
	case 0173:		// Sub exponents
	  nofpu();
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
	  nofpu();
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

      flag_zero = !acc;
      if (trace > 1)
	printf("\tACC:%c%012llo R1:%c%012llo R2:%c%012llo Z:%d\n", WF(acc), WF(r1), WF(r2), flag_zero);
    }
}

int main(void)
{
  parse_in();
  run();
  return 0;
}
