/*
 * AVRDUDE - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2025 Stefan Rueger <stefan.rueger@urclocks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

#include "avrdude.h"
#include <libavrdude.h>
#include "urbootlist.h"
#include "urclock_private.h"

#define Return(...) do { \
  if(silent) \
    return -1; \
  if(verbose > 0 || rethelp) \
    autogen_help(up); \
  pmsg_error("(urboot) "); \
  msg_error(__VA_ARGS__); \
  msg_error("\n"); \
  return -1; \
} while (0)

static int has_alt_spec(int nu, const Uart_conf *uap) {
  for(int i=0; i<nu; i++)
    if(uap[i].alt)
      return 1;
  return 0;
}

static void autogen_help(const Avrintel *up) {
  AVRPART *part = up? locate_part(part_list, up->name): NULL;
  int has_dual = part? urbootexists(part->id, "swio10", "dual", 0): 0;

  msg_error("%s",
    "Bootloader features are specified in an underscore-separated list of the\n"
    "filename in arbitrary order, eg, \"urboot:autobaud_2s\". Features are, eg,\n"
  );
  msg_error("%s", up && up->wdttype == WDT_CLASSIC3?
    "               2s  WDT timeout: 250ms, 500ms, 1s (default) or 2s\n":
    "               2s  WDT timeout: 250ms, 500ms, 1s (default), 2s, 4s or 8s\n"
  );
  if(up && up->numuarts > 0)
    msg_error("%s",
    "         autobaud  Bootloader adapts to host baud rate within MCU capability\n"
    "          uart<n>  Hardware UART number, eg, uart0 (default), uart1, ...\n"
    );
  if(up && up->uarts && has_alt_spec(up->nuartconfs, up->uarts))
    msg_error("%s",
    "           alt<n>  Alternative UART I/O lines\n"
    );
  msg_error("%s",
    "         9.6kbaud  Or other reasonable baud rates; also accepting baud unit\n"
    "            16MHz  Or other f_cpu; also accepting kHz and Hz units\n"
    "      x,i,a-h,j-q  Optional F_cpu prefix designator, eg, i8MHz\n"
  );
  if(verbose > 0)
    msg_error("%s",
    "                   x: external oscillator (default)\n"
    "                   i: internal oscillator\n"
    "                   a-h: internal oscillator that is 10% (a) to 1.25% (h) slow\n"
    "                   j-q: internal oscillator that is 1.25% (j) to 10% (q) fast\n"
    );
  msg_error("%s",
    "             swio  Software I/O, must specify rx and tx pins, see below\n"
    "     rx[a-h][0-7]  MCU receive pin for swio, eg, rxb0\n"
    "     tx[a-h][0-7]  MCU transfer pin for swio, eg, txb1\n"
    "           lednop  If no LED is specified generate template bootloader\n"
    "     no-led/noled  Drop blinking code unless a LED is specified\n"
    "led[+-][a-h][0-7]  Generate code for activity LED with polarity +/-, eg, led+b5\n"
    );
  if(has_dual)
    msg_error("%s",
    "             dual  Dual boot; must specify CS pin for external SPI flash\n"
    "     cs[a-h][0-7]  Chip select pin for dual boot, eg, csd5\n"
    );
  if(up && up->nboots > 0)
    msg_error("%s",
    "               hw  Generate bootloader with hardware boot section\n"
    );
  msg_error("%s",
    "             v<n>  Optional vector for vector b/loader, eg, v25 or vspmready\n"
    "               ee  Generate bootloader with EEPROM r/w support\n"
    "               ce  Generate bootloader that can emulate a chip erase\n"
  );
  if(up && (up->flashsize & (up->flashsize-1)) == 0) // Not ATmega406
    msg_error("%s",
    "               pr  Generate bootloader with reset vector protection\n"
    );
  if(part && part->n_page_erase <= 1) // Not ATtiny441/841/1634/1634R
    msg_error("%s",
    "               u1  Generate bootloader that skips redundant flash page writes\n"
    "               u2  ... and skips redundant flash page erases during emulated CE\n"
    "               u3  ... and skips not needed flash page erases during page write\n"
    "               u4  ... and skips empty flash page writes after page erase\n"
    "                   Note u1..u3 is advisory, ie, can result in any of u1..u4\n"
    );
  msg_error("%s",
    "  serialno=abc123  Put serial number abc123 in top of unused bootloader flash\n"
    "  fill=urboot\\x20  Fill unused bootloader flash repeatedly with argument\n"
    "  save=myfile.hex  Save bootloader to file with chosen name\n"
    "             save  Save bootloader to file with canonical file name\n"
    "  tags=myfile.tag  Save symbols to tag file with chosen name\n"
    "             tags  Save symbols to tag file with canonical file name\n"
    "          configs  Show needed fuse configuration but do not write to memories\n"
    "             show  Show bootloader features but do not write to flash\n"
    "             list  List possible bootloader configurations but do not write\n"
    "             best  Select smallest feature-rich bootloader (first in list)\n"
    "                   and, if baud rate error too high for UART, switch to swio\n"
    "             help  Show this help message and return\n"
    "Features can also be specified like in elements of a canonical file name.\n"
    "For details on urboot bootloaders see https://github.com/stefanrueger/urboot\n"
  );
}

typedef struct {
  int wdt_idx;
  int autobaud, setuart, uart, alt, swio, tx, rx, baudrate, fcpu, fcpu_type;
  int gotbaud, b_value, b_extra, linlbt, linbrrlo, brr;
  int lednop, dual, cs;
  int led, ledpolarity;
  int req_feats, req_ulevel, vecnum, save, tags, configs, show, list, best;
  FILEFMT savefmt;
  Urboot_template *ut;
  char *serialno, *fill, *vectorstr, *savefname, *tagsfname;
  size_t n_serialno, n_fill;
  const char *mcu;
  char iotype[32];
  const Avrintel *up;           // Context about MCU
  int start;                    // Start address of bootloader in flash
  int n_ursegs;                 // Number of file segments used in .hex/.srec file
  Segment ursegs[5];            // At most: Reset vector, bootloader, fill, serial number, table
} Urbootparams;

#define WDT_CLASSIC_WDE (1<<3)
#define WDT_CLASSIC_250MS  (WDT_CLASSIC_WDE | 0x04)
#define WDT_CLASSIC_500MS  (WDT_CLASSIC_WDE | 0x05)
#define WDT_CLASSIC_1S     (WDT_CLASSIC_WDE | 0x06)
#define WDT_CLASSIC_2S     (WDT_CLASSIC_WDE | 0x07)
#define WDT_CLASSIC_4S     (WDT_CLASSIC_WDE | 0x20)
#define WDT_CLASSIC_8S     (WDT_CLASSIC_WDE | 0x21)

static const struct {
  double timeout;
  int wdt_time;
  const char *name;
} wdtopts[] = {
   { 0.25, WDT_CLASSIC_250MS, "250ms" },
   { 0.5,  WDT_CLASSIC_500MS, "500ms" },
   { 1.0,  WDT_CLASSIC_1S, "1s" },
   { 2.0,  WDT_CLASSIC_2S, "2s" },
   { 4.0,  WDT_CLASSIC_4S, "4s" },
   { 8.0,  WDT_CLASSIC_8S, "8s" },
};


#define _ok(c) ((c) > 0 && (c) < 0x7f)

// Is s a ^[0-9]+k[0-9]+$ pattern for baud rate?
static int is_baudrate_k(const char *s) {
  int pre=0, post=0;

  while(_ok(*s) && isdigit((int) *s))
    pre++, s++;
  if(*s != 'k')
    return 0;
  s++;
  while(_ok(*s) && isdigit((int) *s))
    post++, s++;
  return !*s && pre && post;
}

// Is ch a F_cpu type letter?
static int is_fcpu_type(char ch) {
  return ch == 'x' || (ch >= 'a' && ch <= 'q');
}

static const char port_letters[] = "abcdefghjklmnpqr";

// Returns port number in [0, 15] from port letter or -1 if not a port
static int portnum(char letter) {
  const char *q;

  return _ok(letter) && (q = strchr(port_letters, tolower(letter)))? q-port_letters: -1;
}

// Returns port letter from port number or '?' if number out of range
static int portletter(int num) {
  return (unsigned) num >= sizeof port_letters-1? '?': port_letters[num];
}

// Return port name (eg, a0 or b3) in closed-circuit memory
static const char *ccportname(int port) {
  return str_ccprintf("%c%d", portletter(port >> 4), port & 7);
}

// Return upper case port name (eg, A0 or B3) in closed-circuit memory
static const char *ccuportname(int port) {
  return str_ccprintf("%c%d", toupper(portletter(port >> 4)), port & 7);
}

// Is s a ^[a-qx]?[0-9]+m[0-9]+$ pattern for F_cpu?
static int is_fcpu_m(const char *s) {
  int pre=0, post=0;

  if(is_fcpu_type(*s))
    s++;
  while(_ok(*s) && isdigit((int) *s))
    pre++, s++;
  if(*s != 'm')
    return 0;
  s++;
  while(_ok(*s) && isdigit((int) *s))
    post++, s++;
  return !*s && pre && post;
}

// Does s follow a number [kM]unit pattern? Return 1, 1000 or 10000000 depending on prefix
static int is_num_unit(const char *s, const char *unit) {
  int pre = 0, post = 0, ee = 0;

  if(!str_caseends(s, unit))
    return 0;

  if(str_caseeq(unit, "hz") && is_fcpu_type(*s))
    s++;

  while(*s == '+')              // Ignore leading + (used as fillers for sorting)
    s++;
  while(_ok(*s) && isdigit((int) *s))
    pre++, s++;
  if(*s == '.')
    s++;
  while(_ok(*s) && isdigit((int) *s))
    post++, s++;
  if(!pre && !post)
    return 0;

  if(*s == 'e' || *s == 'E') {
    s++;
    if(*s == '-' || *s == '+')
      s++;
    while(_ok(*s) && isdigit((int) *s))
      ee++, s++;
    if(!ee)
      return 0;
  }
  while(_ok(*s) && isspace((int) *s))
    s++;

  size_t ulen = strlen(unit);
  if((*s == 'k' || *s == 'K') && strlen(s) == ulen+1)
    return 1000;
  if((*s == 'm' || *s == 'M') && strlen(s) == ulen+1)
    return 1000*1000;
  return strlen(s) == ulen;
}

// Return 0 if bit-addressable port is available; otherwise show error message and return -1
static int assert_port(int port, const char *what, const char *mcu, int out, const Avrintel *up,
  int rethelp, int silent) {

  if(!up)
    Return("unexpected lack of context info");

  if((port & 0xff) == (PNA & 0xff))
    Return("no %s line specified, add _%s[a-g][0-7]", what, what);

  if(port < 0 || port > 0xf7 || (port & 8))
    Return("unexpected malformed port code %02x", port);

  if(up->nports <= 0 || !up->ports) // Don't know about ports and addresses
    Return("%s: insufficient port info available for %s", what, mcu);

  int pnum = port >> 4, pbit = port & 7;
  for(int i = 0; i < up->nports; i++)
    if(pnum == portnum(up->ports[i].letter)) { // Exists?
      if(out) {                 // Check DDR and PORT register
        if(up->ports[i].dirmask & up->ports[i].outmask & (1<<pbit))
          if(up->ports[i].diraddr < 0x20 && up->ports[i].outaddr < 0x20)
            return 0;
      } else {                  // Check PIN register only
        if(up->ports[i].inmask & (1<<pbit) && up->ports[i].inaddr < 0x20)
          return 0;
      }
    }

  Return("%s does not have bit-addressable %sput port P%s for %s", mcu, out? "out": "in",
    ccuportname(port), what);
}

// Return 0 if bit-addressable port is available; otherwise return -1
static int silent_assert_port(int port, int out, const Avrintel *up) {
  if(!up || port < 0 || port > 0xf7 || (port & 8) || up->nports <= 0 || !up->ports)
    return -1;

  int pnum = port >> 4, pbit = port & 7;
  for(int i = 0; i < up->nports; i++)
    if(pnum == portnum(up->ports[i].letter)) { // Exists?
      if(out) {                 // Check DDR and PORT register
        if(up->ports[i].dirmask & up->ports[i].outmask & (1<<pbit))
          if(up->ports[i].diraddr < 0x20 && up->ports[i].outaddr < 0x20)
            return 0;
      } else {                  // Check PIN register only
        if(up->ports[i].inmask & (1<<pbit) && up->ports[i].inaddr < 0x20)
          return 0;
      }
    }

  return -1;
}

// Cycles per bit given the number of delay loop iterations for software I/O
static long swio_cpb(int val, int is_xmega, int pc_22bit) {
  if(!is_xmega && !pc_22bit)
    return 6L*val + 14+9;       // Classic MCU with 16-bit PC
  if(!is_xmega)
    return 6L*val + 18+9;       // Classic MCU with 22-bit PC
  if(!!pc_22bit)
    return 6L*val + 12+9;       // XMEGA with 16-bit PC

  return 6L*val + 16+9;         // XMEGA with 22-bit PC
}

// Number of delay loop iterations given the cycles per bit
static int swio_b_value(int cpb, int b_off, int is_xmega, int pc_22bit) {
  if(!is_xmega && !pc_22bit)
    return (cpb-14-9+b_off+60)/6-10;  // Classic MCU with 16-bit PC
  if(!is_xmega)
    return (cpb-18-9+b_off+60)/6-10;  // Classic MCU with 22-bit PC
  if(!!pc_22bit)
    return (cpb-12-9+b_off+60)/6-10;  // XMEGA with 16-bit PC

  return (cpb-16-9+b_off+60)/6-10;    // XMEGA with 22-bit PC
}

// Is the baud rate br achievable through SWIO given f_cpu?
static int swio_in_range(const Avrintel *up, long f_cpu, long br) {
  int is_xmega = up->avrarch == F_XMEGA, pc_22bit = up->flashsize > (1<<17);
  long min_br = f_cpu/swio_cpb(256, is_xmega, pc_22bit);
  long max_br = f_cpu/swio_cpb(1, is_xmega, pc_22bit); // @@@ 0 if swio0n templates available
  return br <= max_br && br >= min_br;
}

static int set_swio_params(Urbootparams *ppp, long f_cpu, long brate, int rethelp, int silent) {
  const Avrintel *up = ppp->up;

  // Need a valid rx/tx for swio
  if(assert_port(ppp->rx, "rx", ppp->mcu, 0, up, rethelp, silent) == -1)
    return -1;
  if(assert_port(ppp->tx, "tx", ppp->mcu, 1, up, rethelp, silent) == -1)
    return -1;
  if(ppp->rx == ppp->tx)
    Return("cannot create SW I/O bootloader with RX pin same as TX pin");
  if(!ppp->baudrate)
    Return("SWIO bootloaders need a baud rate, eg, 115k2 or 19200baud");
  if(!f_cpu)
    Return("SWIO bootloaders need a CPU frequency, eg, x16m0 or 8MHz");

  int is_xmega = up->avrarch == F_XMEGA;
  int pc_22bit = up->flashsize > (1<<17);
  // Cycles per tx/rx bit
  long cpb = (f_cpu+brate/2)/brate;
  // Delay loop has granularity of 6 cycles - want around 1% accuracy
  int b_off = cpb > 600?
    3: // 3 centres the error: max error is +/- 3 cycles
    0; // Underestimate b_value and insert opcodes for extra cycles (0..5)
  int b_value = swio_b_value(cpb, b_off, is_xmega, pc_22bit);
  int b_cpb = swio_cpb(b_value, is_xmega, pc_22bit);
  int b_extra = cpb > 600? 0: cpb - b_cpb;

  if(b_value > 256)             // Yes, 256 is still OK
    Return("baud rate too slow for SWIO");
  if(b_value < 0)
    Return("baud rate too fast for SWIO");
  if(b_value == 0)              // @@@ Only because there are no swio0x bootloader templates
     Return("no bootloader template with that SWIO baud rate (compile from source)");
  if(b_extra > 5 || b_extra < 0)
    Return("baud rate incompatible with F_CPU for SWIO");

  ppp->b_value = b_value;
  ppp->b_extra = b_extra;
  ppp->gotbaud = f_cpu/(swio_cpb(b_value, is_xmega, pc_22bit) + b_extra);
  snprintf(ppp->iotype, sizeof ppp->iotype, "swio%d%d", !!b_value, b_extra);

  return 0;
}

// Max value of Baud Rate Register
static int maxbrr(const Avrintel *up) {
  // Only use low byte of LIN baud rate register
  int nbits = up->uarttype == UARTTYPE_LIN? 8: up->brr_nbits;

  return (1 << nbits) - 1;
}

static int rawuartbrr(const Avrintel *up, long f_cpu, long br, int nsamples) {
  switch(up->uarttype) {
  case UARTTYPE_CLASSIC:
  case UARTTYPE_LIN:
    return (f_cpu + nsamples*br/2)/(nsamples*br) - 1;
  }

  return 0;
}

// Baud rate register value given f_cpu, desired baud rated and number 8..63 of samples
static int uartbrr(const Avrintel *up, long f_cpu, long br, int nsamples) {
  int ret = rawuartbrr(up, f_cpu, br, nsamples), mxb = maxbrr(up);

  return ret<0? 0: ret > mxb? mxb: ret;
}

// Actual baud rate given f_cpu, desired baud rated and number 8..63 of samples
double uartbaud(const Avrintel *up, long f_cpu, long br, int nsamples) {
  return (1.0*f_cpu)/(nsamples*(uartbrr(up, f_cpu, br, nsamples) + 1));
}

// Absolute UART quanitisation error in ppm
long absuartqerr(const Avrintel *up, long f_cpu, long br, int nsamples) {
  double bdiff = (uartbaud(up, f_cpu, br, nsamples) - br)*1e6;
  return fabs(bdiff/br) + 0.5;
}

// Return either UART2X=1 or UART2X=0 depending on f_cpu, desired baud rate and preference u2x
static int uart2x(const Avrintel *up, long f_cpu, long br, int u2x) {
  if(!u2x || !up->has_u2x)      // No choice: must use 1x mode
    return 0;
  if(u2x == 2)                  // No choice: must use 2x mode
    return 1;
  /*
   * Part can choose between UART2X = 1 (nsamples == 8) and UART2X = 0 (nsamples == 16) and the
   * user doesn't mind. Switch to 2x mode if error for normal mode is > 1.4% and error with 2x
   * less than normal mode considering that normal mode has higher tolerances than 2x speed mode.
   * The reason for a slight preference for UART2X=0 is that is costs less code in urboot.c.
   */
  long e1 = absuartqerr(up, f_cpu, br, 8), e0 = absuartqerr(up, f_cpu, br, 16);

  return 20*e1 < 15*e0 && e0 > 14000; // e0 > 1.4%
}

// Return l1 or l2, whichever causes less error; on same quantised error return the larger value
static int linbetter2_ns(const Avrintel *up, long f_cpu, long br, int l1, int l2) {
  long e1 = absuartqerr(up, f_cpu, br, l1), e2 = absuartqerr(up, f_cpu, br, l2);
  return e1 < e2? l1: e1 > e2? l2: l1 > l2? l1: l2;
}

// Best of 4
static int linbetter4_ns(const Avrintel *up, long f_cpu, long br, int l1, int l2, int l3, int l4) {
  return linbetter2_ns(up, f_cpu, br,
    linbetter2_ns(up, f_cpu, br, l1, l2),
    linbetter2_ns(up, f_cpu, br, l3, l4));
}

// Best of 8
static int linbetter8_ns(const Avrintel *up, long f_cpu, long br,
  int l1, int l2, int l3, int l4, int l5, int l6, int l7, int l8) {
  return linbetter2_ns(up, f_cpu, br,
    linbetter4_ns(up, f_cpu, br, l1, l2, l3, l4),
    linbetter4_ns(up, f_cpu, br, l5, l6, l7, l8));
}

// Best of all possible ns = 8..63
static int linbest_ns(const Avrintel *up, long f_cpu, long br) {
  return linbetter8_ns(up, f_cpu, br,
    8,
    linbetter8_ns(up, f_cpu, br,  8,  9, 10, 11, 12, 13, 14, 15),
    linbetter8_ns(up, f_cpu, br, 16, 17, 18, 19, 20, 21, 22, 23),
    linbetter8_ns(up, f_cpu, br, 24, 25, 26, 27, 28, 29, 30, 31),
    linbetter8_ns(up, f_cpu, br, 32, 33, 34, 35, 36, 37, 38, 39),
    linbetter8_ns(up, f_cpu, br, 40, 41, 42, 43, 44, 45, 46, 47),
    linbetter8_ns(up, f_cpu, br, 48, 49, 50, 51, 52, 53, 54, 55),
    linbetter8_ns(up, f_cpu, br, 56, 57, 58, 59, 60, 61, 62, 63));
}

// Like str_caseeq(s1, s2) but ignores _ in strings
static int vec_caseeq(const char *s1, const char *s2) {
  int ret;

  do {
    while(*s1 == '_')
      s1++;
    while(*s2 == '_')
      s2++;
    ret = tolower(*s1 & 0xff) == tolower(*s2 & 0xff);
    s2++;
    if(!*s1++)
      break;
  } while(ret);

  return ret;
}

// Set immediate value of cpi/ldi instruction at codep to imm
static void setimm(uint16_t *codep, int imm) {
  *codep = (*codep & 0xf0f0) | (((imm & 0xf0) << 4) | (imm & 0x0f));
}

// Get immediate value from cpi/ldi instruction at codep
static int getimm(uint16_t *codep) {
  return ((*codep & 0x0f00) >> 4)  | (*codep & 0x000f);
}

static void update_insync_ok(const Avrintel *up, uint16_t *insyncp, uint16_t *okp) {
  int insync = getimm(insyncp);
  int ok = getimm(okp);
  if(insync == 255 && ok == 254)
    insync = Resp_STK_INSYNC, ok = Resp_STK_OK;
  else if(ok > insync)
    ok--;

  int16_t bootinfo = insync*255 + ok;
  int urfeatures = UB_FEATURES(bootinfo);

  bootinfo = urfeatures*UB_N_MCU + up->mcuid;
  insync = bootinfo/255;
  ok = bootinfo % 255;
  if(ok >= insync)
    ok++;
  if(insync == Resp_STK_INSYNC && ok == Resp_STK_OK)
    insync = 255, ok = 254;
  setimm(insyncp, insync);
  setimm(okp, ok);
}

static const Port_bits *getportbits(const Avrintel *up, int port) {
  int letter = toupper(portletter(port>>4)); // '?' if port was invalid

  for(size_t i=0; i < up->nports; i++)
    if(up->ports[i].letter == letter)
      return up->ports+i;

   return NULL;
}

static int getdiraddr(const Avrintel *up, int port) {
  const Port_bits *pb = getportbits(up, port);

  return pb && pb->diraddr < 0x20? pb->diraddr: -1;
}

static int getoutaddr(const Avrintel *up, int port) {
  const Port_bits *pb = getportbits(up, port);

  return pb && pb->outaddr < 0x20? pb->outaddr: -1;
}

static int getinaddr(const Avrintel *up, int port) {
  const Port_bits *pb = getportbits(up, port);

  return pb && pb->inaddr < 0x20? pb->inaddr: -1;
}

// Set the I/O register and port bit for sbi, cbi, sbic and sbis opcodes
static void setregbit(const Avrintel *up, uint16_t *codep, int addr, int port) {
  *codep = addr < 0 || addr >= 0x20? 0x0000: // Nop (ok if no DDR reg, eg)
    (*codep & 0xff00) | (addr << 3) | (port & 7);
}

// Return register number n if this is a mov rn, rn template nop; -1 otherwise
static int templateregn(int opcode) {
  // Mov rn, rn looks like 0x2cnn for 0 <= n <= 15
  if((opcode >> 8) != 0x2c || (opcode & 0x0f) != ((opcode & 0xf0) >> 4))
    return -1;

  return opcode & 0x0f;
}

static void portopcode(const Avrintel *up, uint16_t *codep, int regn, int port) {
  int addr;

  switch(regn) {
  case 0:                       // sbi <outaddr>, <bit>
    if((addr = getoutaddr(up, port)) >= 0)
      *codep = 0x9a00 | (addr << 3) | (port & 7);
    break;
  case 1:                       // cbi <outaddr>, <bit>
    if((addr = getoutaddr(up, port)) >= 0)
      *codep = 0x9800 | (addr << 3) | (port & 7);
    break;
  case 2:                       // sbi <diraddr>, <bit>
    if((addr = getdiraddr(up, port)) >= 0)
      *codep = 0x9a00 | (addr << 3) | (port & 7);
    break;
  case 3:                       // out <outaddr>, r1 (reset all PORT bits of port)
    if((addr = getoutaddr(up, port)) >= 0)
      *codep = 0xb810 | ((addr & 0x30) << 5) | (addr & 0x0f);
    break;
  case 4:                       // out <diraddr>, r1 (reset all DDR bits of port)
    if((addr = getdiraddr(up, port)) >= 0)
      *codep = 0xb810 | ((addr & 0x30) << 5) | (addr & 0x0f);
    break;
  }
}

static void fcpuname(char *sp, const Urbootparams *ppp) {
  char *p = sp;

  *p++ = '_';
  if(ppp->fcpu_type)
    *p++ = ppp->fcpu_type;
  sprintf(p, "%.6f", ppp->fcpu/1e6);

  p += strlen(p);
  while(p > sp+1 && p[-2] != '.' && p[-1] == '0')
    *--p = 0;
  while(p >= sp && *p != '.')
    --p;
  if(*p == '.')
    *p = 'm';
}

static void baudname(char *sp, const Urbootparams *ppp) {
  char *p = sp;

  *p++ = '_';
  sprintf(p, "%.3f", ppp->baudrate/1e3);

  p += strlen(p);
  while(p > sp+1 && p[-2] != '.' && p[-1] == '0')
    *--p = 0;
  while(p >= sp && *p != '.')
    --p;
  if(*p == '.')
    *p = 'k';
}

static const char *wdtname(const Urbootparams *ppp) {
  return (unsigned) ppp->wdt_idx >= sizeof wdtopts/sizeof*wdtopts? "nowdt":
    wdtopts[ppp->wdt_idx].name;
}

const Uart_conf *getuartsigs(const Avrintel *up, int uart, int alt) {
  if(up && up->nuartconfs > 0 && up->uarts)
    for(int i = 0; i < up->nuartconfs; i++)
      if(up->uarts[i].uart == uart && up->uarts[i].alt == alt)
        return up->uarts+i;
  return NULL;
}

static const char *ccuartnum(const Urbootparams *ppp) {
  if(ppp->up && ppp->up->avrarch == F_XMEGA)
    return str_ccprintf("%c%d", 'c'+ppp->uart/2, ppp->uart%2);
  return str_ccprintf("%d", ppp->uart);
}

static char *urboot_filename(const Urbootparams *ppp, const char *ext) {
  char *ret = mmt_malloc(1024), *p = ret;

  sprintf(p, "urboot_%s_%s", ppp->mcu, wdtname(ppp));
  if(ppp->autobaud)             // if(ppp->ut->table[2] & UR_AUTOBAUD)
    strcpy((p+=strlen(p)), "_autobaud");
  else {
    fcpuname((p+=strlen(p)), ppp), baudname((p+=strlen(p)), ppp);
    if(ppp->swio) {
      sprintf((p+=strlen(p)), "_swio_rx%c%d_tx%c%d",
        portletter(ppp->rx >> 4), ppp->rx & 7, portletter(ppp->tx >> 4), ppp->tx & 7);
    }
  }

  if(!ppp->swio) {
    sprintf((p+=strlen(p)), "_uart%s", ccuartnum(ppp));
    if(ppp->alt)
      sprintf((p+=strlen(p)), "_alt%d", ppp->alt);

    const Uart_conf *usigs = getuartsigs(ppp->up, ppp->uart, ppp->alt);
    if(usigs && usigs->rxd != (uint8_t) PNA)
      sprintf((p+=strlen(p)), "_rx%c%d", portletter(usigs->rxd >> 4), usigs->rxd & 7);
    if(usigs && usigs->txd != (uint8_t) PNA)
      sprintf((p+=strlen(p)), "_tx%c%d", portletter(usigs->txd >> 4), usigs->txd & 7);
  }

  if(ppp->led != PNA)
    sprintf((p+=strlen(p)), "_led%c%c%d", ppp->ledpolarity == -1? '-': '+', portletter(ppp->led >> 4), ppp->led & 7);
  if(ppp->cs != PNA)
    sprintf((p+=strlen(p)), "_cs%c%d", portletter(ppp->cs >> 4), ppp->cs & 7);

  if(ppp->dual)
    strcpy((p+=strlen(p)), "_dual");
  else if(ppp->led == PNA)
    strcpy((p+=strlen(p)), ppp->lednop? "_lednop": "_no-led");

  if(ppp->ut) {
    if(!ppp->ut->features && !ppp->ut->update_level)
      strcpy((p+=strlen(p)), "_min");
    if(ppp->ut->features & URFEATURE_EE)
      strcpy((p+=strlen(p)), "_ee");
    if(ppp->ut->features & URFEATURE_CE)
      strcpy((p+=strlen(p)), "_ce");
    if(ppp->ut->update_level)
      sprintf((p+=strlen(p)), "_u%d", ppp->ut->update_level);
    if(ppp->ut->features & URFEATURE_HW)
      strcpy((p+=strlen(p)), "_hw");
    else {
      if(ppp->ut->features & URFEATURE_PR)
         strcpy((p+=strlen(p)), "_pr");
      if(ppp->vectorstr)
        sprintf((p+=strlen(p)), "_v%s", ppp->vectorstr);
    }
  }

  if(ppp->n_serialno && ppp->serialno)
    strcpy((p+=strlen(p)), "_serialno");
  if(ppp->n_fill && ppp->fill)
    strcpy((p+=strlen(p)), "_fill");
  strcpy((p+=strlen(p)), ext);

  if(p-ret >= 1024)             // This may never happen: panic
    exit(123);

  return ret;
}

typedef struct {
  int addr, loc; const char *name, *com;
} Tagsym;

static int symaddrcmp(const void *p1, const void *p2) {
  return ((Tagsym *) p1)->addr - ((Tagsym *) p2)->addr;
}

static void urboot_write_tagsfile(const Urbootparams *ppp) {
  if(!ppp || !ppp->tagsfname || !ppp->ut || !ppp->up) {
    pmsg_error("unexpectedly missing %s\n",
      !ppp? "ppp": !ppp->tagsfname? "tagsfname": !ppp->ut? "ut": "up");
    return;
  }

  Tagsym sy[] = {
    { 0, UL_JMP_APPLICATION,  "application",      NULL },
    { 0, UL_RJMP_APPLICATION, "application",      NULL },
    { 0, UL_LDI_STK_OK,       "send_ok",          NULL },
    { 0, UL_LDI_WDTO,         "serial_boot",      "Serial port bootloading" },
    { 0, UL_BITDELAY,         "bit_delay",        NULL },
    { 0, UL_HALFBITDELAY,     "halfbit_delay",    NULL },
    { 0, UL_EEPROM_READ,      "eeprom_read",      NULL },
    { 0, UL_EEPROM_WRITE,     "eeprom_write",     NULL },
    { 0, UL_GETADDRLENGTH,    "get_addr_buflen",  NULL },
    { 0, UL_GETCH,            "getch",            NULL },
    { 0, UL_GET_SYNC,         "get_sync",         NULL },
    { 0, UL_NOT_RESET_PAGE,   "not_reset_page",   NULL },
    { 0, UL_PGM_WRITE_PAGE,   "pgm_write_page",   NULL },
    { 0, UL_PUTCH,            "putch",            NULL },
    { 0, UL_QEXITERR,         "quick_error_exit", NULL },
    { 0, UL_RWW_ENABLE,       "rww_enable",       NULL },
    { 0, UL_SFM_BOOT,         "sfm_boot",         "SPI flash memory bootloading" },
    { 0, UL_SKIP_ERASE,       "skip_erase",       NULL },
    { 0, UL_SPI_TRANSFER,     "spi_transfer",     NULL },
    { 0, UL_UB_SPM,           "ub_spm",           "Generic SPM interface" },
    { 0, UL_WATCHDOGCONFIG,   "set_watchdog",     NULL },
    { 0, UL_WRITEBUFFERX,     "writebufferX",     NULL },
  };

  int start = ppp->start, bsize = ppp->ut->size, usage = ppp->ut->usage;
  int fsz = ppp->up->flashsize, vecsz = fsz <= 8192? 2: 4;
  int top = start + usage - 1, awd = top > 0xffff? 5: 4;
  int vecnum = (ppp->ut->table[0] >> 8) & 0x7f;
  int npages = ppp->ut->table[0] & 0x7f;
  uint16_t *locs = ppp->ut->locs;

  int payload = start+bsize-6, remain = usage - bsize;
  int nserial, sitems, filled, fitems, nwd = 1, tmp;

  if(remain < 0) {
    pmsg_error("unexpected bootloader size %d and usage %d\n", bsize, usage);
    return;
  }

  nserial = 0;                  // Serial number has priority over filling unused area
  if(ppp->n_serialno && ppp->serialno) {
    if((nserial = ppp->n_serialno) > remain)
      nserial = remain;
    remain -= nserial;
  }
  sitems = nserial & 1? nserial: nserial/2;
  if(sitems && nwd < (tmp = strlen(str_ccprintf("%d", sitems))))
    nwd = tmp;

  filled = ppp->n_fill && ppp->fill;
  fitems = remain & 1? remain: remain/2;
  if(fitems && (nserial || filled) && nwd < (tmp = strlen(str_ccprintf("%d", fitems))))
    nwd = tmp;

  // Assign symbol addresses and sort them in ascending order
  for(size_t i = 0; i < sizeof sy/sizeof*sy; i++)
    sy[i].addr = locs[sy[i].loc]? start+2*locs[sy[i].loc]: 0;
  qsort(sy, sizeof sy/sizeof*sy, sizeof *sy, symaddrcmp);

  FILE *fp = str_eq(ppp->tagsfname, "-")? stdout: fopen(ppp->tagsfname, "w");
  if(!fp) {
    pmsg_ext_error("unable to open %s: %s\n", ppp->tagsfname, strerror(errno));
    return;
  }

  // Vector table usage by the bootloader
  fprintf(fp,   "0x%0*x L %*s __vector_reset\n", awd, 0, nwd+2, "");
  if(vecnum && vecsz == 4 && usage < 4096 && (fsz & (fsz-1)) == 0)
    fprintf(fp, "0x%0*x P B %*d urmarker         Urboot marker bytes\n", awd, 2, nwd, 2);
  if(vecnum)
    fprintf(fp, "0x%0*x L %*s __vector_%d\n", awd, vecnum*vecsz, nwd+2, "", vecnum);

  fprintf(fp,   "0x%0*x L %*s urboot           Entry point for bootloader\n", awd, start, nwd+2, "");

  for(size_t i = 0; i < sizeof sy/sizeof*sy; i++)
    if(sy[i].addr && !sy[i].com)
      fprintf(fp, "0x%0*x L %*s %s\n", awd, sy[i].addr, nwd+2, "", sy[i].name);
    else if(sy[i].addr)
      fprintf(fp, "0x%0*x L %*s %-*s %s\n", awd, sy[i].addr, nwd+2, "", 16, sy[i].name, sy[i].com);

  if(fitems && (nserial || filled))
    fprintf(fp, "0x%0*x P %c %*d fill             %s\n",
      awd, payload, remain & 1? 'B': 'W', nwd, fitems,
      filled? "Area set once at burn time": "Unused area by bootloader");

  if(nserial)
    fprintf(fp, "0x%0*x P %c %*d serialno         Set once at burn time\n",
      awd, payload+remain, nserial & 1? 'B': 'W', nwd, sitems);

  if(remain && !nserial && !filled)
    fprintf(fp, "0x%0*x L %*s unused           %d bytes for _serialno= and/or _fill=\n",
      awd, payload, nwd+2, "", usage - bsize);

  // Version table
  fprintf(fp,   "0x%0*x P B %*d npages_vecnum    Usage of %d pages; %s bootloader\n", awd, top-5, nwd, 2,
    npages, ppp->ut->type);
  fprintf(fp,   "0x%0*x P B %*d features_version Encodes %s\n", awd, top-1, nwd, 2,
    ppp->ut->urversion);

  if(!str_eq(ppp->tagsfname, "-"))
    fclose(fp);
}

// Return temporary buffer with features that the user needs to add for this selection
static const char *ccselection(int dfeat, int ulevel) {
  char buf[64] = { 0 }, *p = buf;

  if(dfeat & URFEATURE_EE)
    strcpy((p+=strlen(p)), "_ee");
  if(dfeat & URFEATURE_CE)
    strcpy((p+=strlen(p)), "_ce");
  if(dfeat & URFEATURE_U4)
    strcpy((p+=strlen(p)), "_u4");
  else if(ulevel)
    sprintf((p+=strlen(p)), "_u%d", ulevel);
  if(dfeat & URFEATURE_HW)
    strcpy((p+=strlen(p)), "_hw");
  else if(dfeat & URFEATURE_PR)
    strcpy((p+=strlen(p)), "_pr");
  if(!*buf)
    strcpy(buf, "-->");

  return str_ccprintf("%s", buf);
}

// Returns whether the template closely matches the requested properties
static int urmatch(Urboot_template *ut, int req_feats, int req_ulevel) {
  int uf = ut->features, rf = req_feats, ru = req_ulevel;
  if(uf & URFEATURE_HW)
    uf &= ~URFEATURE_PR;
  if(rf & URFEATURE_HW)
    rf &= ~URFEATURE_PR;
  if(rf != uf)
    return 0;
  return ru == 0 || ru == 4? 1: ut->update_level == ru;
}

static int urbootautogen_parse(const AVRPART *part, char *urname, Urbootparams *ppp, int silent) {
  char *p, *q, *tok;
  int idx, factor, ns, pnum, beyond = 0, rethelp = 0;
  const Avrintel *up = NULL;

  memset(ppp, 0, sizeof *ppp);
  ppp->wdt_idx = 2;             // Default to 1 s WDT timeout
  ppp->fcpu_type = 'x';         // Default to external oscillator
  ppp->rx = ppp->tx = PNA;      // Invalidate rx/tx pins
  ppp->cs = PNA;                // Invalidate cs pin
  ppp->led = PNA;               // Invalidate led pin
  ppp->savefmt = FMT_IHXC;      // Saved file defaults to Intel Hex with comments

  if((idx = upidxmcuid(part->mcuid)) < 0)
    Return("part %s does not have uP_table entry", ppp->mcu);
  up = uP_table + idx;

  ppp->up = up;
  ppp->mcu = part->id;

  // Quick and dirty attempt at gleaning a help further down the line
  rethelp = (q = strstr(urname, "_help")) && q[-1] != '\\' && strchr("._", q[5]);

  if(!str_starts(urname, "urboot:"))
    Return("%s does not start with urboot:", urname);

  // Remove hex unless last para is save=...; first find last unescaped underscore
  if((p = strrchr(urname, '_'))) {
    do {
      for(q = p--; *p == '\\'; --p)
        continue;
      if((q-p) & 1)             // Even number of escape chars
        break;
      while(*p != '_' && p > urname + 6)
        --p;
    } while(*p == '_');         // Check again for unescaped underscore
  }
  if(!p || !str_starts(q, "_save="))
    if(str_caseends(urname, ".hex"))
      urname[strlen(urname)-4] = 0;

  p = urname + 7;
  if(!*p) {
    if(!silent)
      autogen_help(up);
    return -1;
  }
  while(*(tok = str_nexttok(p, "_", &p))) {
    if(!beyond++) {             // Accept part only as first element
      AVRPART *urpart = locate_part(part_list, tok);
      if(urpart) {
        if(!str_eq(ppp->mcu, urpart->id))
          Return("-p %s part is incompatible with urboot:%s name", part->desc, tok);
        continue;
      }
    }

    if((factor = is_num_unit(tok, "s"))) { // WDT timeout
      int wdt_idx = -1;
      double tm;

      if(factor == 1000 || sscanf(tok, "%lf", &tm) != 1) // No kiloseconds
        Return("cannot parse %s for wdt timeout", tok);

      if(factor == 1000*1000)   // ms (not Megaseconds)
        tm /= 1000;
      for(size_t i=0; i<sizeof wdtopts/sizeof*wdtopts; i++)
        if(tm > wdtopts[i].timeout*0.9 && tm < wdtopts[i].timeout*1.1)
          wdt_idx = i;
      if(wdt_idx == -1)
        Return("%s wdt timeout not close to any of 250 ms ... 8 s", tok);

      ppp->wdt_idx = wdt_idx;
      continue;
    }

    if(str_eq(tok, "autobaud")) {
      ppp->autobaud = 1;
      continue;
    }

    if(str_eq(tok, "swio")) {
      ppp->swio = 1;
      continue;
    }

    size_t tlen = strlen(tok);
#define tk(n) (tlen >= (n)? tok[n]: 0)
    int t2 = tk(2), t3 = tk(3), t4 = tk(4), t5 = tk(5), t6 = tk(6);
#undef tk
    if(str_starts(tok, "uart") && t4 >= '0' && t4 <= '9' && !t5) {
      ppp->setuart = 1;
      ppp->uart = t4 - '0';
      continue;
    }

    if(is_pdi(part)) {          // XMEGAs number uarts UARTC0, UARTC1, UARTD0, ... UARTF1
      int l4 = _ok(t4)? tolower(t4): '?';
      if(str_starts(tok, "uart") && l4 >= 'c' && l4 <= 'f' && t5 >= '0' && t5 <= '1' && !t6) {
        ppp->setuart = 1;
        ppp->uart = (l4 - 'c')*2 + t5 - '0';
        continue;
      }
    }

    if(str_starts(tok, "alt") && t3 >= '0' && t3 <= '9' && !t4) {
      ppp->setuart = 1;
      ppp->alt = t3 - '0';
      continue;
    }

    if(str_starts(tok, "tx") && (pnum = portnum(t2)) >= 0 && t3 >= '0' && t3 <= '7' && !t4) {
      ppp->tx = pnum*16 + t3 - '0';
      continue;
    }

    if(str_starts(tok, "rx") && (pnum = portnum(t2)) >= 0 && t3 >= '0' && t3 <= '7' && !t4) {
      ppp->rx = pnum*16 + t3 - '0';
      continue;
    }

    if(str_starts(tok, "cs") && (pnum = portnum(t2)) >= 0 && t3 >= '0' && t3 <= '7' && !t4) {
      ppp->cs = pnum*16 + t3 - '0';
      continue;
    }

    if(str_starts(tok, "led") && (t3 == '+' || t3 == '-') &&
      (pnum = portnum(t4)) != -1 && t5 >= '0' && t5 <= '7' && !t6) {
      ppp->led = pnum*16 + t5 - '0';
      ppp->ledpolarity = t3 == '+'? 1: -1;
      continue;
    }

    if(str_starts(tok, "led") && (pnum = portnum(t3)) != -1 && t4 >= '0' && t4 <= '7' && !t5) {
      ppp->led = pnum*16 + t4 - '0';
      ppp->ledpolarity = 1;
      continue;
    }

    if(is_baudrate_k(tok)) {
      if(!(q=strchr(tok, 'k')))
        Return("unexpected baud rate %s", tok);

      *q = '.';
      double bd;
      ns = sscanf(tok, "%lf", &bd);
      *q = 'k';

      if(ns != 1)
        Return("cannot parse baud rate %s", tok);
      if(bd < 0.0095 || bd >= 8000.0005)
        Return("baud rate %s out of bounds [0k01, 8000k0]", tok);
      ppp->baudrate = (10000*bd+5)/10;
      continue;
    }

    if((factor = is_num_unit(tok, "baud"))) {
      while(*tok == '+')
        tok++;

      double bd;
      if(sscanf(tok, "%lf", &bd) != 1)
        Return("cannot parse baud rate %s", tok);

      bd *= factor;
      if(bd < 9.5 || bd >= 8000*1000+0.5)
        Return("baud rate %s out of bounds [0.01 kbaud, 8000 kbaud]", tok);
      ppp->baudrate = (10*bd+5)/10;
      continue;
    }

    if(is_fcpu_m(tok)) {
      if(is_fcpu_type(*tok))
        ppp->fcpu_type = *tok++;

      if(!(q=strchr(tok, 'm')))
        Return("unexpected F_cpu %s", tok);

      *q = '.';
      double fq;
      ns = sscanf(tok, "%lf", &fq);
      *q = 'm';

      if(ns != 1)
        Return("cannot parse F_cpu %s", tok);

      if(fq < 0.0009995 || fq >= 64.0000005)
        Return("F_cpu %s out of bounds [0m001, 64m0]", tok);

      ppp->fcpu = (10*1000*1000*fq+5)/10;
      continue;
    }

    if((factor = is_num_unit(tok, "hz"))) {
      if(is_fcpu_type(*tok))
        ppp->fcpu_type = *tok++;

      while(*tok == '+')
        tok++;

      double fq;
      if(sscanf(tok, "%lf", &fq) != 1)
        Return("cannot parse F_cpu %s", tok);

      fq *= factor;
      if(fq < 999.5 || fq >= 64e6+0.5)
        Return("F_cpu %s out of bounds [1 kHz, 64 MHz]", tok);

      ppp->fcpu = (10*fq+5)/10;
      continue;
    }

    if(str_eq(tok, "dual")) {
      if(!urbootexists(part->id, "swio10", "dual", 0))
        Return("there are no dual bootloaders for %s", part->desc);
      ppp->dual = 1;
      continue;
    }

    if(str_eq(tok, "lednop")) {
      ppp->lednop = 1;
      continue;
    }

    // Ignore no-led or noled request: automatically assigned when no LED requested
    if(str_eq(tok, "no-led") || str_eq(tok, "noled"))
      continue;

    if(str_eq(tok, "min"))
      continue;

    if(str_eq(tok, "pr")) {
      ppp->req_feats |= URFEATURE_PR;
      continue;
    }
    if(str_eq(tok, "ce")) {
      ppp->req_feats |= URFEATURE_CE;
      continue;
    }
    if(str_eq(tok, "ee")) {
      ppp->req_feats |= URFEATURE_EE;
      continue;
    }
    if(str_eq(tok, "hw")) {
      ppp->req_feats |= URFEATURE_HW;
      continue;
    }

    if(str_starts(tok, "fill")) {
      if((q = strchr(tok, '='))) {
        ppp->fill = mmt_strdup(q + 1);
        ppp->n_fill = cfg_unescapen((unsigned char *) ppp->fill, (unsigned char *) ppp->fill);
      }
      continue;
    }

    if(str_starts(tok, "serialno")) {
      if((q = strchr(tok, '='))) {
        ppp->serialno = mmt_strdup(q + 1);
        ppp->n_serialno = cfg_unescapen((unsigned char *) ppp->serialno, (unsigned char *) ppp->serialno);
      }
      continue;
    }

    if(*tok == 'u' && tlen == 2 && strchr("01234", tok[1])) {
      ppp->req_ulevel = tok[1] - '0';
      continue;
    }

    if(*tok == 'v') {
      ppp->vectorstr = str_lc(mmt_strdup(tok+1));
      continue;
    }

    if(str_starts(tok, "save")) {
      ppp->save = 1;
      if(tok[4] == '=') {
        ppp->savefname = mmt_strdup(tok+5);
        cfg_unescape(ppp->savefname, ppp->savefname);
        size_t fnlen = strlen(ppp->savefname);
        if(fnlen > 2 && ppp->savefname[fnlen-2] == ':') {
          ppp->savefname[fnlen-2] = 0;
          FILEFMT sfmt = fileio_format_with_errmsg(ppp->savefname[fnlen - 1], "");
          if(sfmt == FMT_ERROR)
            return -1;
          if(sfmt != FMT_AUTO)
            ppp->savefmt = sfmt;
        }
        if(!*ppp->savefname) {
          mmt_free(ppp->savefname);
          ppp->savefname = NULL;
        }
      }
      continue;
    }

    if(str_starts(tok, "tags")) {
      ppp->tags = 1;
      if(tok[4] == '=') {
        ppp->tagsfname = mmt_strdup(tok+5);
        cfg_unescape(ppp->tagsfname, ppp->tagsfname);
        if(!*ppp->tagsfname) {
          mmt_free(ppp->tagsfname);
          ppp->tagsfname = NULL;
        }
      }
      continue;
    }

    if(str_eq(tok, "configs")) {
      ppp->configs = 1;
      continue;
    }

    if(str_eq(tok, "show")) {
      ppp->show = 1;
      continue;
    }

    if(str_eq(tok, "list")) {
      ppp->list = 1;
      continue;
    }

    if(str_eq(tok, "best")) {
      ppp->best = 1;
      continue;
    }

    if(str_eq(tok, "help")) {
      if(!silent)
        autogen_help(up);
      return -1;
    }

    Return("unable to parse _%s segment", tok);
  }

  if(ppp->req_ulevel == 4)
    ppp->req_feats |= URFEATURE_U4;

  if(ppp->req_feats & URFEATURE_HW)
    ppp->req_feats &= ~URFEATURE_PR;

  if(ppp->vectorstr) {
    if(ppp->req_feats & URFEATURE_HW)
      Return("cannot specify vector when HW supported bootloader selected");
    int vecnum = -2;
    if(looks_like_number(ppp->vectorstr)) {
      const char *errptr;
      int num = str_int(ppp->vectorstr, STR_INT32, &errptr);
      if(errptr)
        Return("v%s: %s", ppp->vectorstr, errptr);
      vecnum = num == -1? up->ninterrupts: num;
    } else if(vec_caseeq(ppp->vectorstr, "ADDITIONAL_VECTOR")) {
      vecnum = up->ninterrupts;
    } else if(up->isrtable) {
      for(int i=0; i < up->ninterrupts; i++)
        if(vec_caseeq(up->isrtable[i], ppp->vectorstr)) {
          vecnum = i;
          break;
        }
    }
    if(vecnum == 0)
      Return("Cannot use RESET vector for vector bootloader");
    if(vecnum < 0 || vecnum > up->ninterrupts)
      Return("vector %s not known for %s", ppp->vectorstr, part->desc);
    ppp->vecnum = vecnum;
    if(up->isrtable) {          // Replace vector string with ISR name from isrtable[]
      mmt_free(ppp->vectorstr);
      ppp->vectorstr = str_vectorname(up, vecnum);
    }
  }

  if(up->wdttype == WDT_CLASSIC3 && ppp->wdt_idx > 4)
    Return("unable to set WDT of %c s (%s has a max wdt time of 2 s)", *wdtopts[ppp->wdt_idx].name, part->desc);

  // Compute configuration of template bootloader
  const char *cfg = ppp->lednop? "lednop": "noled";

  if(ppp->dual) {
    if(assert_port(ppp->cs, "cs", part->desc, 1, up, rethelp, silent) == -1)
      return -1;
    cfg = "dual";
  }

  if(ppp->led != PNA && assert_port(ppp->led, "led", part->desc, 1, up, rethelp, silent) == -1)
    return -1;
  if(str_eq(cfg, "noled") && ppp->led != PNA) // Override noled if led explicitly requested
    cfg = "lednop";

  // Compute I/O type of template bootloader

  long f_cpu = ppp->fcpu, brate = ppp->baudrate;
  if(f_cpu && ppp->fcpu_type >= 'a' && ppp->fcpu_type <= 'q') // Adjust F_cpu in 1.25% steps
    f_cpu = (f_cpu * (10000LL + 125*(ppp->fcpu_type - 'i')))/10000;

  if(up->numuarts <= 0)         // Default to SWIO in absence of UARTS
    ppp->swio = 1;

  // Check and/or set rx/tx pins from uart<n>
  if(!ppp->swio || ppp->setuart) {
    const Uart_conf *usigs = getuartsigs(up, ppp->uart, ppp->alt);
    if(up && up->nuartconfs > 0 && up->uarts && !usigs)
      Return("unknown uart%s%s for %s",
       ccuartnum(ppp), ppp->alt? str_ccprintf("_alt%d", ppp->alt): "", part->desc);
    if(usigs && usigs->rxd != (uint8_t) PNA) {
      if(ppp->rx != PNA && usigs->rxd != ppp->rx)
        Return("uart%s%s expects rx%s not rx%s",
          ccuartnum(ppp), ppp->alt? str_ccprintf("_alt%d", ppp->alt): "",
          ccportname(usigs->rxd), ccportname(ppp->rx));
      ppp->rx = usigs->rxd;
    }
    if(usigs && usigs->txd != (uint8_t) PNA) {
      if(ppp->tx != PNA && usigs->txd != ppp->tx)
        Return("UART expects tx%s not tx%s", ccportname(usigs->txd), ccportname(ppp->tx));
      ppp->tx = usigs->txd;
    }
  }

  if(ppp->autobaud) {
    if(up->numuarts <= 0)
      Return("autobaud requires the part to have UART I/O, but %s doesn't", part->desc);
    if(ppp->swio)
      Return("Cannot use SW I/O with autobaud bootloaders");
    snprintf(ppp->iotype, sizeof ppp->iotype, "autobaud_uart%d%s",
      ppp->uart, ppp->alt? str_ccprintf("_alt%d", ppp->alt): "");
  } else if(ppp->swio) {
    if(set_swio_params(ppp, f_cpu, brate, rethelp, silent) == -1)
      return -1;
  } else { // UART
    if(!ppp->baudrate)
      Return("missing autobaud or a baud rate, eg, 115k2 or 19200baud");
    if(!f_cpu)
      Return("missing autobaud or a CPU frequency, eg, x16m0 or i8MHz");
    if(up->uarttype == UARTTYPE_LIN) {
      if(f_cpu > brate*64L*(maxbrr(up)+1)) // Quantisation error 64/63, ie, ca 1.6%
        Return("baud rate too small for 8-bit LINBRR");
      if(f_cpu < 79L*brate/10L) // Quantisation error 79/80, ie, ca 1.25%
        Return("baud rate too big for LIN UART");
      int ns = linbest_ns(up, f_cpu, brate);
      ppp->linlbt = 0x80 | ns;
      ppp->linbrrlo = uartbrr(up, f_cpu, brate, ns);
      ppp->gotbaud = uartbaud(up, f_cpu, brate, ns);
      snprintf(ppp->iotype, sizeof ppp->iotype, "lin_uart%d", ppp->uart);
    } else if(up->uarttype == UARTTYPE_CLASSIC) {
      int smp = uart2x(up, f_cpu, brate, 1)? 8: 16; // Choose 2x or 1x depending on error
      int raw = rawuartbrr(up, f_cpu, brate, smp);
      int mxb = maxbrr(up);
      if(raw > mxb)
        Return("unachievable baud rate (too slow)");
      if(raw < 0)
        Return("unachievable baud rate (too fast)");
      ppp->brr = raw;
      ppp->gotbaud = uartbaud(up, f_cpu, brate, smp);
      snprintf(ppp->iotype, sizeof ppp->iotype, "u%dx%d_uart%d%s",
        smp == 8? 2: 1, raw > 255? 12: 8,
        ppp->uart, ppp->alt? str_ccprintf("_alt%d", ppp->alt): "");
    } else
      Return("%s UART not (yet) implemented", part->desc);
  }

  // Analyse baud rate error and switch to SWIO if possible
  if(ppp->fcpu && ppp->baudrate && ppp->gotbaud) {
    double signerr = 100.0*(ppp->gotbaud - ppp->baudrate)/ppp->baudrate, bauderr = fabs(signerr);
    int warned = 0;

    if((ppp->fcpu_type != 'x' && bauderr > 0.7) || bauderr > 2.2) {
      if(!ppp->swio && swio_in_range(up, f_cpu, brate) &&
        silent_assert_port(ppp->rx, 0, up) == 0 &&
        silent_assert_port(ppp->tx, 1, up) == 0) {

        if(ppp->best || !ppp->setuart) { // Switch to SWIO
          pmsg_notice("switching to SWIO as baud rate error %.2f%% too high for %s oscillator\n",
            signerr, ppp->fcpu_type == 'x'? "external": "internal");
          if(set_swio_params(ppp, f_cpu, brate, rethelp, silent) == -1)
            return -1;
          ppp->swio = 1;
          signerr = 100.0*(ppp->gotbaud - ppp->baudrate)/ppp->baudrate, bauderr = fabs(signerr);
          if((ppp->fcpu_type != 'x' && bauderr > 0.7) || bauderr > 2.2) {
            warned = 1;
            pmsg_warning("baud rate error %.2f%% for %s oscillator still too high\n",
              signerr, ppp->fcpu_type == 'x'? "external": "internal");
          }
        } else {
          warned = 1;
          pmsg_warning("high baud rate error %.2f%% for %s oscillator: consider switching to swio\n",
            signerr, ppp->fcpu_type == 'x'? "ext": "int");
        }
      } else {
        warned = 1;
        pmsg_warning("baud rate error %.2f%% for %s oscillator too high\n",
          signerr, ppp->fcpu_type == 'x'? "external": "internal");
      }
    }
    if(!warned)
      pmsg_notice("baud rate error %.2f%% for %s oscillator OK\n",
        signerr, ppp->fcpu_type == 'x'? "external": "internal");
  }

  uint16_t *locations, *bootloader, *versiontable, size, usage;
  Urboot_template **urlist;
  int nut = 0;

  urlist = urboottemplate(up, ppp->mcu, ppp->iotype, cfg, ppp->req_feats, ppp->req_ulevel,
    ppp->list || ppp->best, &nut, silent);

  if(!urlist || nut == 0)
    return -1;

  if(ppp->list && !silent) {
    size_t maxtype = 0, maxver = 14, maxuse = 3;
    int add[32] = { 0 }, maxd = 0, addone = 0, alldiff = 0;
    int sw=0, se=0, sU=0, sd=0, sj=0, sh=0, sP=0, sr=0, sa=0, sc=0, sp=0, sm=0;

    for(int n=0; n<nut; n++) {
      char *t = ppp->vectorstr && !(urlist[n]->features & URFEATURE_HW)? "vector": urlist[n]->type;
      if(strlen(t) > maxtype)
        maxtype = strlen(t);
      if(strlen(urlist[n]->urversion) > maxver)
        maxver = strlen(urlist[n]->urversion);
      if(urlist[n]->usage > 999)
        maxuse = 4 + (urlist[n]->usage > 9999);
      int fdiff = (ppp->req_feats ^ urlist[n]->features) & 31;
      if((int) bitcount(fdiff) > maxd)
        maxd = bitcount(fdiff);
      if(add[fdiff]++)
        addone = 1;
      for(char *p = strchr(urlist[n]->urversion, ' '); p && *p; p++)
        switch(*p) {
        case 'w': sw=1; break;
        case 'e': se=1; break;
        case 'U': sU=1; break;
        case 'd': sd=1; break;
        case 'j': sj=1; break;
        case 'h': sh=1; break;
        case 'P': sP=1; break;
        case 'r': sr=1; break;
        case 'a': sa=1; break;
        case 'c': sc=1; break;
        case 'p': sp=1; break;
        case '-': sm=1; break;
        }
      alldiff |= fdiff;
    }

    if(!(maxd += addone))
      maxd++;
    maxd *= 3;

    term_out("%*.*s Size %*sUse Vers%s Features  Type%*s Canonical file name\n",
      maxd, maxd, "Selection", (int) maxuse-3, "", maxver < 15? "": "i", (int) maxtype-4, "");
    for(int use=0, n=0; n<nut; n++) {
      ppp->ut = urlist[n];
      char *p = urboot_filename(ppp, ".hex");
      char *t = ppp->vectorstr && !(urlist[n]->features & URFEATURE_HW)? "vector": urlist[n]->type;
      int fdiff = (ppp->req_feats ^ urlist[n]->features) & 31;
      term_out("%*.*s %c%3d %*d %*s %-*s %s\n",
        maxd, maxd, ccselection(fdiff, add[fdiff] > 1? urlist[n]->update_level: 0),
        use != urlist[n]->usage? '*': ' ', urlist[n]->size,
        (int) maxuse, urlist[n]->usage, (int) maxver, urlist[n]->urversion, (int) maxtype, t, p);
      mmt_free(p);
      use = urlist[n]->usage;
    }

    if(verbose <= 0)
      term_out("\nA higher verbosity level shows more about features and selection\n");
    else {
      term_out("\n"
        "      * Indicates the most feature-rich bootloader given flash usage\n"
        "Size    Bootloader code size\n"
        "Use     Flash usage of bootloader (" // )
      );
      if(sh)
        term_out("boot section");
      if(sh && sj)
        term_out(" or ");
      if(sj)
        term_out("multiple of page size");
      term_out(                 // (
        ")\n"
        "Vers    Urboot bootloader version\n"
        "Type    Hardware or vector bootloader\n"
        "Canonical file name is used when saving via _save\n"
        "Feature Bootloader capabilites\n"
      );
      if(sw)
       term_out("  w provides pgm_write_page(sram, flash) for the application at FLASHEND-4+1\n");
      if(se)
       term_out("  e supports EEPROM r/w\n");
      if(sU)
       term_out("  U skips redundant flash page writes/erases\n");
      if(sd)
       term_out("  d dual boot (over the air programming from external SPI flash\n");
      if(sj)
       term_out("  j vector bootloader\n");
      if(sP)
       term_out("  P protects bootloader and reset vector from being overwritten\n");
      if(sp)
       term_out("  p protects bootloader from being overwritten\n");
      if(sh)
       term_out("  h hardware-supported bootloader\n");
      if(sr)
       term_out("  r preserves reset flags for the application in the register R2\n");
      if(sa)
       term_out("  a autobaud detection (f_cpu/8n using discrete divisors, n = 1, 2, ..., 256)\n");
      if(sc)
       term_out("  c bootloader provides chip erase functionality\n");
      if(sm)
       term_out("  - corresponding feature not present\n");
      if(alldiff) {
        term_out("Selection\n");
        if(alldiff & URFEATURE_HW)
          term_out("  _hw Hardware-supported bootloaders only\n");
        if(alldiff & URFEATURE_PR)
          term_out("  _pr Reset vector must be protected\n");
        if(alldiff & URFEATURE_EE)
          term_out("  _ee Bootloader must handle EEPROM r/w\n");
        if(alldiff & URFEATURE_CE)
          term_out("  _ce Bootloader must handle Chip Erase commands\n");
        if(alldiff & URFEATURE_U4)
          term_out(
            "  _u1  Bootloader skips redundant flash page writes\n"
            "  _u2  ... and skips redundant flash page erases during emulated CE\n"
            "  _u3  ... and skips not needed flash page erases during page write\n"
            "  _u4  ... and skips empty-flash page writes after page erase\n"
            "  Note u1..u3 is advisory, ie, can result in any of u1..u4\n"
        );
      }
    }
  }

  ppp->ut = *urlist;
  if(!ppp->best)
    for(int n = nut-1; n >= 0; n--) // Downgrade to requested ulevel if needed
      if(urmatch(urlist[n], ppp->req_feats, ppp->req_ulevel))
        ppp->ut = urlist[n];

  // Deallocate urlist
  for(int n=0; n<nut; n++) {
    if(urlist[n] && urlist[n] != ppp->ut) {
      mmt_free(urlist[n]->tofree);
      mmt_free(urlist[n]);
    }
  }
  mmt_free(urlist);
  nut = 0;

  if(ppp->list)
    return -1;

  size = ppp->ut->size;
  usage = ppp->ut->usage;

  if(up->flashsize <= 0)
    Return("unexpected flash size %d for %s", up->flashsize, part->desc);

  locations = ppp->ut->locs;
  bootloader = ppp->ut->code;
  versiontable = ppp->ut->table;

  ppp->start = up->flashsize - usage;

  int loc, locok;
  // Parametrise the bootloader
  for(int i=0; i < UL_PARAMETERS_N; i++) {
    if((loc = locations[i])) {
      switch(i) {
      case UL_LDI_BRRLO:
        setimm(bootloader+loc, ppp->brr);
        break;
      case UL_LDI_BRRHI:
        setimm(bootloader+loc, ppp->brr >> 8);
        break;
      case UL_LDI_BRRSHARED:
        setimm(bootloader+loc, (ppp->brr >> 8) << 4);
        break;
      case UL_LDI_LINBRRLO:
        setimm(bootloader+loc, ppp->linbrrlo);
        break;
      case UL_LDI_LINLBT:
        setimm(bootloader+loc, ppp->linlbt);
        break;
      case UL_SWIO_EXTRA12:
        switch(ppp->b_extra) {
          case 1: bootloader[loc] = 0x0000; break; // Nop
          case 2: bootloader[loc] = 0xC000; break; // Rjmp .+0
          default: Return("unexpected b_extra value %d", ppp->b_extra);
        }
        break;
      case UL_LDI_BVALUE:
        setimm(bootloader+loc, ppp->b_value);
        break;
      case UL_LDI_WDTO:
        if((unsigned) ppp->wdt_idx >= sizeof wdtopts/sizeof*wdtopts)
          Return("unexpected wdt_idx %d", ppp->wdt_idx);
        setimm(bootloader+loc, wdtopts[ppp->wdt_idx].wdt_time);
        break;
      case UL_LDI_STK_INSYNC:   // Also treat UL_LDI_STK_OK here
        if(!(locok = locations[UL_LDI_STK_OK]))
          Return("unexpectedly missing code point for ldi_stk_ok");
        update_insync_ok(up, bootloader+loc, bootloader+locok);
        break;
      case UL_LDI_STK_OK:       // Already treated above
        break;
      case UL_RJMP_APPLICATION:
        if(ppp->vecnum)         // Jump forward over FLASHEND into the vector table
          bootloader[loc] = rjmp_opcode(ppp->vecnum*(up->flashsize <= 8192? 2: 4) + usage - loc*2, up->flashsize);
        break;
      case UL_JMP_APPLICATION:
        if(ppp->vecnum)
          uint32tobuf((unsigned char *) bootloader+loc, jmp_opcode(ppp->vecnum*4));
        break;
      case UL_SBI_DDRTX:
        setregbit(up, bootloader+loc, getdiraddr(up, ppp->tx), ppp->tx);
        break;
      case UL_CBI_TX:
      case UL_SBI_TX:
        setregbit(up, bootloader+loc, getoutaddr(up, ppp->tx), ppp->tx);
        break;
      case UL_SBIC_RX_START:
      case UL_SBIC_RX:
        setregbit(up, bootloader+loc, getinaddr(up, ppp->rx), ppp->rx);
        break;
      case UL_LDI_STARTHHZ:
        setimm(bootloader+loc, ppp->start >> 16);
        break;
      case UL_LDI_STARTHI:
      case UL_CPI_STARTHI:
        setimm(bootloader+loc, ppp->start >> 8);
        break;
      case UL_CPI_STARTLO:
        setimm(bootloader+loc, ppp->start);
        break;
      default:
        Return("unexpected code location %d for parameter", i);
      }
    }
  }

  // Ensure version table contains new vector number
  if(ppp->vecnum > 0) {
    if(ppp->vecnum > 127)
      Return("unexpected vector number %d > 127", ppp->vecnum);
    versiontable[0] = (versiontable[0] & 0x80ff) | (ppp->vecnum << 8);
  }

  // Replace Template opcodes
  for(int i = 0; i < (size-6)/2; i++) {
    int opcode = bootloader[i], regn;

    if(is_opcode32(opcode)) {
      i++;
      continue;
    }

    switch((regn = templateregn(opcode))) {
    default:
      continue;
    case 0:
    case 1:
      portopcode(up, bootloader+i, regn ^ (ppp->ledpolarity == -1), ppp->led);
      break;
    case 12:
      portopcode(up, bootloader+i, 2, ppp->led);
      break;

    case 3:
    case 4:
      portopcode(up, bootloader+i, regn, ppp->led);
      break;

    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
      portopcode(up, bootloader+i, regn-5, ppp->cs);
      break;
    }
  }

  return 0;
}

int urboot_has_contents(const AVRPART *part, const char *filename) {
  Urbootparams pp;
  char *urname = mmt_strdup(filename);

  // Silently parse the urboot:... string
  int ret = urbootautogen_parse(part, urname, &pp, 1) == 0 && !(pp.show || pp.configs);

  mmt_free(urname);
  mmt_free(pp.serialno);
  mmt_free(pp.fill);
  mmt_free(pp.vectorstr);
  mmt_free(pp.savefname);
  mmt_free(pp.tagsfname);
  if(pp.ut) {
    mmt_free(pp.ut->tofree);
    mmt_free(pp.ut);
  }

  return ret;
}

// Is the part configuration valid in principle?
static int is_config_valid(int nc, const Configitem *cfg, const char *str) {
  char *cs = mmt_strdup(str), *vs = NULL, *p;
  int ret = 0;

  if((p = strchr(cs, '=')))
     *p = 0, vs = p+1;

  for(int i = 0; i < nc; i++) {
    if(str_eq(cfg[i].name, cs)) {
      if(!vs || looks_like_number(vs))
        ret = 1;
      else
        for(int j = 0; j < cfg[i].nvalues; j++)
          if(str_eq(vs, cfg[i].vlist[j].label)) {
            ret = 1;
            break;
          }
      break;
    }
  }

  mmt_free(cs);
  return ret;
}

static void handle_cfg(const PROGRAMMER *pgm, const AVRPART *part, const char *cfg) {
  char line[200];
  snprintf(line, sizeof line, "config %s%s", verbose > 0? "-c ": "", cfg);

  if(pgm)
    terminal_line(pgm, part, line);
  else
    term_out("%s\n", line);
}

static void classic_configuration(const Avrintel *up, const PROGRAMMER *pgm, const AVRPART *part, int hwusage) {
  /*
   * Set configuration properties if they exist
   *
   * All classic bootloaders:
   *   - config lb=no_lock
   *   - config blb0=no_lock_in_app
   *   - config rstdisbl=external_reset
   *   - config selfprgen=spm_enabled
   *
   * Vector bootloaders:
   *   - config blb1=no_lock_in_boot
   *   - config bootrst=application
   *   - config bootsz=3
   *
   * Hardware-supported bootloaders:
   *   - config blb1=spm_disabled_in_boot
   *   - config bootrst=boot_section
   *   - config bootsz=bs_512w/256w/128w
   */
  handle_cfg(pgm, part, "lb=no_lock");
  if(is_config_valid(up->nconfigs, up->cfgtable, "blb0"))
    handle_cfg(pgm, part, "blb0=no_lock_in_app");
  if(up->nboots > 0)
    handle_cfg(pgm, part, hwusage? "blb1=spm_disabled_in_boot": "blb1=no_lock_in_boot");
  if(is_config_valid(up->nconfigs, up->cfgtable, "rstdisbl"))
    handle_cfg(pgm, part, "rstdisbl=external_reset");
  if(is_config_valid(up->nconfigs, up->cfgtable, "selfprgen"))
    handle_cfg(pgm, part, "selfprgen=spm_enabled");
  if(up->nboots > 0) {
    if(hwusage) {
      handle_cfg(pgm, part, "bootrst=boot_section");
      if(up->nboots > 1) {
        char bootsz[128];
        snprintf(bootsz, sizeof bootsz, "bootsz=bs_%dw", hwusage/2); // Words
        handle_cfg(pgm, part, bootsz);
      }
    } else {
      handle_cfg(pgm, part, "bootrst=application");
      if(up->nboots > 1)
        handle_cfg(pgm, part, "bootsz=3");
    }
  }
}

// Show (pgm == NULL) or set (pgm != NULL) configurations for autogenerated urboot:... bootloaders
int urbootfuses(const PROGRAMMER *pgm, const AVRPART *part, const char *filename) {
  int ret = -1, rc;
  Urbootparams pp;
  char *urname = mmt_strdup(filename);

  // Silently parse the urboot:... string
  if((rc = urbootautogen_parse(part, urname, &pp, 1) < 0))
    goto done;

  if(is_classic(part))
    classic_configuration(pp.up, pgm, part, pp.ut->features & URFEATURE_HW? pp.ut->usage: 0);

done:
  mmt_free(urname);
  mmt_free(pp.serialno);
  mmt_free(pp.fill);
  mmt_free(pp.vectorstr);
  mmt_free(pp.savefname);
  mmt_free(pp.tagsfname);
  if(pp.ut) {
    mmt_free(pp.ut->tofree);
    mmt_free(pp.ut);
  }

  return ret;
}

// Set memory to autogenerated urboot bootloader as if read from file
int urbootautogen(const AVRPART *part, const AVRMEM *mem, const char *filename) {
  int ret = -1, msize = mem->size;
  Urbootparams pp;
  char *urname = mmt_strdup(filename);

  if(urbootautogen_parse(part, urname, &pp, 0) < 0)
    goto done;

  int bsize = pp.ut->size, usage = pp.ut->usage;
  size_t remain = usage - bsize;
  unsigned char *bloader = (unsigned char *) pp.ut->code;

  if(!mem_is_flash(mem)) {
    pmsg_error("(urboot) can only write bootloader to flash, not %s\n", mem->desc);
    goto done;
  }
  if(msize != pp.up->flashsize) {
    pmsg_error("(urboot) unexpected %s size 0x%04x vs 0x%04x\n", mem->desc, msize, pp.up->flashsize);
    goto done;
  }
  if(usage < bsize) {
    pmsg_error("(urboot) unexpected bootloader size size %d exceeds usage %d\n", bsize, usage);
    goto done;
  }
  if(usage > pp.up->flashsize-4) {
    pmsg_error("(urboot) unexpected bootloader size %d does not fit into flash\n", usage);
    goto done;
  }

  memset(mem->buf, 0xff, msize);
  memset(mem->tags, 0, msize);

  pp.n_ursegs = 0;

  // Reset vector for vector bootloader
  if(!(pp.ut->features & URFEATURE_HW)) { // Vector bootloader: Add r/jmp to bootloader at Reset
    int vecsz;

    pp.ursegs[pp.n_ursegs].addr = 0;
    pp.ursegs[pp.n_ursegs].len = vecsz = msize <= 8192? 2: 4;
    pp.n_ursegs++;

    if(vecsz == 2 || (usage < 4096 &&  (msize & (msize-1)) == 0)) { // Rjmp
      uint16tobuf(mem->buf, rjmp_bwd_blstart(pp.start, msize));
      if(vecsz == 4)
        uint16tobuf(mem->buf+2, 0x7275);
    } else {
      uint32tobuf(mem->buf, jmp_opcode(pp.start));
    }
    memset(mem->tags, TAG_ALLOCATED, vecsz);
  }

  // Bootloader code
  pp.ursegs[pp.n_ursegs].addr = pp.start;
  pp.ursegs[pp.n_ursegs].len = bsize - 6;
  pp.n_ursegs++;
  memcpy(mem->buf + pp.start, bloader, bsize - 6);
  memset(mem->tags + pp.start, TAG_ALLOCATED, bsize - 6);

  // Filler section
  if(pp.n_fill && pp.fill && remain <= pp.n_serialno)
    pp.n_fill = 0;
  if(pp.n_fill && pp.fill) {
    int addr = pp.start + bsize - 6, len = 0;
    for(char *p = pp.fill, *q = (char *) mem->buf+addr; remain > pp.n_serialno; remain--) {
      len++;
      *q++ = *p++;
      if(p >= pp.fill+pp.n_fill)
        p = pp.fill;
    }
    memset(mem->tags + addr, TAG_ALLOCATED, len);
    pp.ursegs[pp.n_ursegs].addr = addr;
    pp.ursegs[pp.n_ursegs].len = len;
    pp.n_ursegs++;
  }

  // Serial Number/Piggy back section
  if(pp.n_serialno && pp.serialno && !remain) {
    pmsg_warning("bootloader has no space left for serialno; ignoring serialno\n");
    pp.n_serialno = 0;
  }
  if(pp.n_serialno && pp.serialno) {
    int len = pp.n_serialno, off = 0, addr = pp.start + usage - 6 - len;
    if(remain < pp.n_serialno) {
      off = pp.n_serialno - remain;
      addr += off;
      len = remain;
      pmsg_warning("serialno exceeds free bootloader space; cutting off first %d bytes\n", off);
    }
    pp.ursegs[pp.n_ursegs].addr = addr;
    pp.ursegs[pp.n_ursegs].len = len;
    pp.n_ursegs++;
    memcpy(mem->buf + addr, pp.serialno + off, len);
    memset(mem->tags + addr, TAG_ALLOCATED, len);
  }

  // Version and bootloader features table
  pp.ursegs[pp.n_ursegs].addr = msize - 6;
  pp.ursegs[pp.n_ursegs].len = 6;
  pp.n_ursegs++;
  memcpy(mem->buf + msize - 6, bloader + bsize - 6, 6);
  memset(mem->tags + msize - 6, TAG_ALLOCATED, 6);

  if(pp.save) {
    if(!pp.savefname)
      pp.savefname = urboot_filename(&pp, ".hex");
    pmsg_notice("writing autogenerated bootloader to %s\n", pp.savefname);
    AVRMEM *memwrite = avr_dup_mem(mem);
    fileio_segments(FIO_WRITE, pp.savefname, pp.savefmt, part, memwrite, pp.n_ursegs, pp.ursegs);
    avr_free_mem(memwrite);
  }

  if(pp.tags) {
    if(!pp.tagsfname)
      pp.tagsfname = urboot_filename(&pp, ".tag");
    pmsg_notice("writing symbols file to %s\n", pp.tagsfname);
    urboot_write_tagsfile(&pp);
  }

  if(pp.show) {
    char *p = urboot_filename(&pp, ".hex");

    if(verbose > 0)
      term_out("Size %*sUse Vers%s Features  Type%*s Canonical file name\n", usage > 9999? 2: usage > 999, "",
        strlen(pp.ut->urversion) < 15? "": "i", (int) strlen(pp.ut->type)-4, "");
    term_out("%*d %d %s %s %s\n", 3+(verbose > 0), bsize, usage, pp.ut->urversion, pp.ut->type, p);
    mmt_free(p);

    memset(mem->buf, 0xff, msize);
    memset(mem->tags, 0, msize);
  }

  if(pp.configs) {
    if(is_classic(part))
      classic_configuration(pp.up, NULL, part, pp.ut->features & URFEATURE_HW? pp.ut->usage: 0);
    memset(mem->buf, 0xff, msize);
    memset(mem->tags, 0, msize);
  }

  ret = pp.show || pp.configs? 0: msize;

done:
  mmt_free(urname);
  mmt_free(pp.serialno);
  mmt_free(pp.fill);
  mmt_free(pp.vectorstr);
  if(pp.ut) {
    mmt_free(pp.ut->tofree);
    mmt_free(pp.ut);
  }

  return ret;
}
