#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "mpglib.h"

#define EOF (-1)

void usage(char *text);
void help(void);

int mpeg(unsigned char secondbyte);
int layer(unsigned char secondbyte);
int sampfreq(unsigned char secondbyte, unsigned char thirdbyte);
int channelmode(unsigned char fourthbyte);
int channels(unsigned char fourthbyte);
int bitrate(unsigned char secondbyte,unsigned char thirdbyte,unsigned char fourthbyte);
int paddingbit(unsigned char thirdbyte);
int framesize(unsigned char secondbyte,unsigned char thirdbyte,unsigned char fourthbyte);
int is_header(int secondbyte, int thirdbyte, int fourthbyte);

long nextframe(long seekpos);
long prevframe(long seekpos);

int showmins (long bytes);
//double showsecs (long bytes);

long fforward (long seekpos,long skiptime);
long frewind (long seekpos,long skiptime);

real avbitrate(void);
long get_total_frames(void);
unsigned int volume(long playpos);

long seeksilence(long seekpos);
long seeksilstart(long seekpos);

/* This function prints the reached ID3 V1 TAG,
   argument is next byte after tag */
void showtag(long seekpos);

/* This function prints the reached ID3 V1 TAG */
void print_id3v1(long seekpos);

/* This function prints the reached ID3 V2 TAG */
void print_id3v2(long showpos);

/* This function rewinds to before ID3V1 tag, if any.
   Returns position of last byte before tag. */
long rewind3v1(long seekpos);

/* This function rewinds max 100kB to before ID3V2 tag, if any.
   Returns position of first byte of tag. */
long rewind3v2(long seekpos);

/* This function gets data from ID3 V1 and returns its length,
   argument is end of tag */
int importid3v1(long seekpos);

/* This function gets data from ID3 V2 and returns its length,
   argument is start of tag */
long importid3v2(long seekpos);

/* This function skips ID3 V1 tag, if any.
   Returns position of next byte after tag. */
long skipid3v1(long seekpos);

/* This function skips ID3 V2 tag, if any.
   Useful also for getting the length of the ID3 V2 tag.
   Returns position of next byte after tag. */
long skipid3v2(long seekpos);

/* This function returns the position of the next ID3 TAG,
   next byte after V1 and first byte of prepended V2 */
long seektag(long seekpos);


void infotag(long seekpos);

void zaptitle (void);

void writeconf(void);
void readconf(void);

void playsel(long int playpos);
void playfirst(long int playpos);

//void savesel(void);
void savewithtag(void);

//void cutexact(void);

//void writetable(void);
//void cutfromtable(void);

void showfileinfo(int rawmode, int fix_channels);
