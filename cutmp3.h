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
double showsecs (long bytes);

long fforward (long seekpos,long skiptime);
long frewind (long seekpos,long skiptime);

double avbitrate(void);
long get_total_frames(void);
unsigned int volume(long playpos);

long seeksilence(long seekpos);
long seeksilstart(long seekpos);

long importid3(long seekpos);
long skipid3v2(long seekpos);
long showid3(long showpos);
void copyid3(long startpos, long endpos);

void infotag(long seekpos);
int importtag();
long seektag(long seekpos);
void showtag(long seekpos);

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
