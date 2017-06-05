/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#define nop() do{}while(0)

#define COL(c) "\e[" c "m"

#define CNON        COL("")
/* Foreground */
#define CBLACK      COL("30")
#define CRED        COL("31")
#define CGREEN      COL("32")
#define CYELLOW     COL("33")
#define CBLUE       COL("34")
#define CPURPLE     COL("35")
#define CCYAN       COL("36")
#define CWHITE      COL("37")
#define CLBLACK     COL("90")
#define CLRED       COL("91")
#define CLGREEN     COL("92")
#define CLYELLOW    COL("93")
#define CLBLUE      COL("94")
#define CLPURPLE    COL("95")
#define CLCYAN      COL("96")
#define CLWHITE     COL("97")
/* Background */
#define CBBLACK     COL("40")
#define CBRED       COL("41")
#define CBGREEN     COL("42")
#define CBYELLOW    COL("43")
#define CBBLUE      COL("44")
#define CBPURPLE    COL("45")
#define CBCYAN      COL("46")
#define CBWHITE     COL("47")
#define CBLBLACK    COL("100")
#define CBLRED      COL("101")
#define CBLGREEN    COL("102")
#define CBLYELLOW   COL("103")
#define CBLBLUE     COL("104")
#define CBLPURPLE   COL("105")
#define CBLCYAN     COL("106")
#define CBLWHITE    COL("107")
/* Generic */
#define CRGB(x)     COL("48;5;"x)
#define CBRGB(x)    COL("38;5;"x)
/* Style */
#define CLIGHT      COL("1")
#define CDIM        COL("2")
#define CUNDERLINE  COL("4")
#define CBLINK      COL("5")
#define CINVERT     COL("7")
#define CCONCEAL    COL("8")
/* Cancel Style */
#define CCDIM       COL("22")
#define CCLIGHT     COL("21")
#define CCUNDERLINE COL("24")
#define CCBLINK     COL("25")
#define CCINVERT    COL("27")
#define CCCONCEAL   COL("28")
/* Reset */
#define CREGULAR    COL("0")

#define COL_MEM "\e[1;34m"
#define COL_IMP "\e[1;31m"
#define COL_REQ "\e[1;32m"
#define COL_PER "\e[1;28m"
#define COL_PIP "\e[1;36m"
//#define COL_RX  "\e[42;30m"
#define COL_DES "\e[43;30m"
#define COL_DEF "\e[0;0m"

#define set_colour(x) printf(x);


#define cprintf(col, ...) do { \
        set_colour(col);       \
        printf(__VA_ARGS__);  \
        set_colour(COL_DEF);   \
    }while(0)


/* Memory related issues */
#ifdef DEBUG_MEM
#define DBG_MEM(...) do { cprintf(COL_MEM, __VA_ARGS__); }while(0)
#else
#define DBG_MEM(...) nop()
#endif
/* Verbose requests */
#ifdef DEBUG_REQ
#define DBG_REQ(...) do { cprintf(COL_REQ, __VA_ARGS__); }while(0)
#else
#define DBG_REQ(...) nop()
#endif
/* Performance issues */
#ifdef DEBUG_PER
#define DBG_PER(...) do { cprintf(COL_PER, __VA_ARGS__); }while(0)
#else
#define DBG_PER(...) nop()
#endif
/* Pipe issues */
#ifdef DEBUG_PIP
#define DBG_PIP(...) do { cprintf(COL_PIP, __VA_ARGS__); }while(0)
#else
#define DBG_PIP(...) nop()
#endif
/* Descriptors */
#ifdef DEBUG_DES
#define DBG_DES(...) do { cprintf(COL_DES, __VA_ARGS__); }while(0)
#else
#define DBG_PIP(...) nop()
#endif
/* Descriptors long */
#ifdef DEBUG_DESLONG
#define DBG_DESL(...) do { cprintf(COL_DES, __VA_ARGS__); }while(0)
#else
#define DBG_PIP(...) nop()
#endif


#endif /* _DEBUG_H_ */
