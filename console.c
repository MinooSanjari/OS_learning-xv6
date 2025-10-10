// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

// Arrow key codes 
#define KEY_HOME 0xE0
#define KEY_END  0xE1
#define KEY_UP   0xE2
#define KEY_DN   0xE3
#define KEY_LF   0xE4  // Left arrow
#define KEY_RT   0xE5  // Right arrow


static void consputc(int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

// helper
static int
get_hwcurs(void)
{
  int pos;
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);
  return pos;
}

static void
set_hwcurs(int pos)
{
  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos & 0xff);
}


static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  } else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  cgaputc(c);
}

#define INPUT_BUF 128
struct {
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
  uint pos;
} input;

#define C(x)  ((x)-'@')  // Control-x

void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      doprocdump = 1;
      break;
    case C('U'):  // Kill line.
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        consputc(BACKSPACE);
      }
      input.pos = input.e - input.w;
      break;

    case C('H'): case '\x7f':  // Backspace
      if(input.pos > 0){
        int len = input.e - input.w;   
        int pos = input.pos;          
        int hw = get_hwcurs();        
        int start_hw = hw - 1;         
        int i;
        for(i = pos - 1; i < len - 1; i++){
          input.buf[(input.w + i) % INPUT_BUF] = input.buf[(input.w + i + 1) % INPUT_BUF];
        }
        input.e--;      
        input.pos--;   
        if(start_hw < 0) start_hw = 0;
        set_hwcurs(start_hw);
        for(i = pos - 1; i < (input.e - input.w); i++){
          consputc(input.buf[(input.w + i) % INPUT_BUF]);
        }
        consputc(' ');
        set_hwcurs(start_hw);
      }
      break;

  
    case KEY_LF:  // Left arrow
      if(input.pos > 0){
        input.pos--;
        {
          int hw = get_hwcurs();
          if(hw > 0)
            set_hwcurs(hw - 1);
        }
      }
      break;

    case KEY_RT:  // Right arrow  
      if(input.pos < (input.e - input.w)){
        {
          int hw = get_hwcurs();
          set_hwcurs(hw + 1);
        }
        input.pos++;
      }
      break;

    case C('D'):  // Ctrl+D: move cursor to start of next word (or EOF if line empty)
      if (input.e == input.w) {
        input.w = input.e;
        wakeup(&input.r);
      } else {
        int len = input.e - input.w;  
        int pos = input.pos;           
        int i = pos;
        while (i < len) {
          char ch = input.buf[(input.w + i) % INPUT_BUF];
          if (ch == ' ' || ch == '\t') break;
          i++;
        }
        while (i < len) {
          char ch = input.buf[(input.w + i) % INPUT_BUF];
          if (ch != ' ' && ch != '\t') break;
          i++;
        }
        if (i != pos) {
          int delta = i - pos;   
          input.pos = i;
          int hw = get_hwcurs();
          set_hwcurs(hw + delta);
        }
      }
      break;


    default:
      if(c != 0 && input.e - input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        int len = input.e - input.w;   
        int pos = input.pos;           
        if(pos < len){
          int i;
          for(i = len; i > pos; i--){
            input.buf[(input.w + i) % INPUT_BUF] = input.buf[(input.w + i - 1) % INPUT_BUF];
          }
          input.buf[(input.w + pos) % INPUT_BUF] = c;
          input.e++;   
          input.pos++;   
          int hw = get_hwcurs();
          int newlen = input.e - input.w;
          int j;
          for(j = pos; j < newlen; j++){
            consputc(input.buf[(input.w + j) % INPUT_BUF]);
          }
          set_hwcurs(hw + 1);
        } else {
          // --- append at end (simple case) ---
          input.buf[input.e++ % INPUT_BUF] = c;
          input.pos++;
          consputc(c);
        }
        if(c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;

    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();
  }
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input.r == input.w){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  
  // Reset cursor position after reading a line
  input.pos = 0;
  
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;\
  input.pos = 0;

  ioapicenable(IRQ_KBD, 0);
}

