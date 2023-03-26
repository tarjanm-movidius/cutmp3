/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/*
	main.c   Cut MP3s framewise, VBR supported!
	MP2 files should work since 2.0.4, as well!
	The code is somewhat ugly and unsafe, sorry! At least it is fast :-)
	(c) Jochen Puchalla 2003-2011  <mail@puchalla-online.de>
*/

#include "cutmp3.h"

#define VERSION "2.1"
#define YEAR "2011"

/* general buffersize */
#define BUFFER 32767
/* 10 MB for VBR scanning */
#define SCANAREA 10485759

	unsigned int silencelength=1000, silvol=1;

	FILE *mp3file, *timefile;
	unsigned char begin[BUFFER];
	long filesize, dataend, inpoint, outpoint;
	long startmins, endmins;
	double startsecs, endsecs;
	int negstart=1, negend=1; /* start- and endpoint from end? */
	int totalmins, audiobegin=0;
	int howlong=1; /* play how many seconds? */
	double totalsecs; /* seconds of total time (<60) */
	int fix_secondbyte=0, fix_sampfreq=0, fix_channelmode=7, fix_mpeg=7;
	long double fix_frametime=0;
	int vbr=0, a_b_used=0;
	double avbr, msec; /* average bitrate and one millisecond in relation to avbr */

	char *filename;
	char *forcedname;
	char *userin;
	char *userout;

	int livetime=1;
	int exactmode=0, mute=0, forced_file=0, overwrite=0, forced_prefix=0;
	int copytags=0, nonint=0;
	long silfactor=15; /* silence already longer than wanted by this factor */
	int stdoutwrite=0;
	long framenumber=0, framecount=0;
	long double totaltime=0;
	int no_tags=0;

	int card=1;
	signed int genre=-1;

	char artist[255]="";
	char album[255]="";
	char year[255]="";
	char comment[255]="";
	/* title is used to determine whether we name the file "result000x.mp3"
	or "artist - title.mp3", so leave it empty here! */
	char title[255]="";

	int hastable=0;
	unsigned char ttable[BUFFER];
	FILE *tblfile;
	char tblname[17] = "/tmp/tableXXXXXX";

	FILE *id3file1;
	FILE *id3file2;
	char id3name1[15] = "/tmp/id1XXXXXX";
	char id3name2[15] = "/tmp/id2XXXXXX";
	int fd1=-1;
	int fd2=-1;

	FILE *logfile;
	char logname[21] = "/tmp/mpg123logXXXXXX";
	int fdlog=-1;

	int debug=0;

void exitseq(int foobar)
{
 	remove(id3name1);
 	remove(id3name2);
 	remove(logname);

	exit(foobar);  /* now really exit */
}

void usage(char *text)
{
	printf("\ncutmp3  version %s  \n", VERSION);
	printf("\nUsage:  cutmp3 -i file.mp3 [-a inpoint] [-b outpoint] [-f timetable]");
	printf("\n               [-o outputprefix] [-e] [-c] [-C] [-q]\n");
	printf("\n  cutmp3 -i file.mp3 -a 0:37 -b 3:57  copies valid data from 0:37 to 3:57");
	printf("\n  cutmp3 -i file.mp3 -f timetable     copies valid data described in timetable");
	printf("\n  cutmp3 -i file.mp3 -o song          writes the output to song0001.mp3, song0002.mp3,...");
	printf("\n  cutmp3 -i file.mp3 -O song.mp3      writes the output to song.mp3");
	printf("\n  cutmp3 -i file.mp3 -O -             writes the output to STDOUT");
	printf("\n  cutmp3 -i file.mp3 -d 2             use second sound card");
	printf("\n  cutmp3 -i file.mp3 -s 0             no maximum silence length");
	printf("\n  cutmp3 -I file.mp3 [-F]             prints file information [in raw mode]\n\n");
	if (text && text[0]!='\0') printf("%s\n\n",text);
}

void help(void)
{
	printf("\n\ncutmp3  version %s  \n", VERSION);
	printf("\n        Seek with 1234567890,.\n");
	printf("        Mark beginning with 'a', go there with 'A'\n");
	printf("        Mark       end with 'b', go there with 'B'\n");
	printf("        Save selection with 's'.\n");
	printf("        Save selection with ID3-tag with 't'.\n\n");
	printf("        Seek to next ID3-tag with 'T'\n");
	printf("        Seek to end of next silence with 'p'\n");
	printf("        Seek to beginning of next silence with 'P'\n");
	printf("        increase volume of silence with '+'.\n");
	printf("        decrease volume of silence with '-'.\n");
	printf("        increase length of silence with 'm'.\n");
	printf("        decrease length of silence with 'n'.\n");
	printf("        increase playback length with 'M'.\n");
	printf("        decrease playback length with 'N'.\n");
	printf("        'o' turns on overwriting for next time.\n");
	printf("        'i' shows file info.\n\n");
	printf("        Replay with 'r'.\n");
	printf("        Toggle playing up to / from location with 'R'.\n");
	printf("        Toggle sound on/off with '#'.\n");
	printf("        Toggle live time on/off with 'l'.\n");
	printf("        Write configuration file with 'S'.\n\n");
	printf("        Press 'q' to quit.\n\n");
	printf("        HINTS: 'w' is a shortcut for 'bsa'.\n");
	printf("               'W' is a shortcut for 'bta'.\n");
	return;
}


/* Which MPEG-Type ? */
/* Type is in bits nr. 11&12 */
int mpeg(unsigned char mpegnumber)
{
//	printf("  mpeg()");

	mpegnumber = mpegnumber << 3;
	mpegnumber = mpegnumber >> 6;
//	printf("\nmpegnumber:%u",mpegnumber);
	if (mpegnumber==0) return 3; /* MPEG2.5 bits are 00 */
	if (mpegnumber==3) return 1; /* MPEG1   bits are 11 */
	if (mpegnumber==2) return 2; /* MPEG2   bits are 10 ; 01 is reserved */
	return 0;
}


int crc(unsigned char crcbyte)
{
//	printf("  crc()");

	crcbyte = crcbyte << 7;
	crcbyte = crcbyte >> 7;
	/* if bit is _not_ set, there is a 16 bit CRC after the header! */
	return 1-crcbyte;
}


int sampfreq(unsigned char secondbyte, unsigned char thirdbyte)
{
	unsigned char sampfreqnumber;
	int MPEG=mpeg(secondbyte);

//	printf("  sampfreq()");

	sampfreqnumber=thirdbyte;
	sampfreqnumber=sampfreqnumber<<4;
	sampfreqnumber=sampfreqnumber>>6;

	if (MPEG==1)
	{
		if (sampfreqnumber==0) return 44100;
		if (sampfreqnumber==1) return 48000;
		if (sampfreqnumber==2) return 32000;
	}
	if (MPEG==2)
	{
		if (sampfreqnumber==0) return 22050;
		if (sampfreqnumber==1) return 24000;
		if (sampfreqnumber==2) return 16000;
	}
	if (MPEG==3) /* MPEG2.5 */
	{
		if (sampfreqnumber==0) return 11025;
		if (sampfreqnumber==1) return 12000;
		if (sampfreqnumber==2) return 8000;
	}
//	printf("\nsecbyte:%u mpeg:%u sampfreqnumber:%u",secondbyte,MPEG,sampfreqnumber);

	/* In case of an error, sampfreq=1 is returned */
	return 1;
}


int channelmode(unsigned char fourthbyte)
{
//	printf("  channelmode()");

	return fourthbyte>>6;
/* result: 0=Stereo
           1=Joint-Stereo
           2=Dual-Channel
           3=Mono */
}


int channels(unsigned char fourthbyte)
{
	if (channelmode(fourthbyte)==3) return 1;
	else return 2;
}


int is_header(int secondbyte, int thirdbyte, int fourthbyte)
{
	if (sampfreq(secondbyte,thirdbyte)==fix_sampfreq && framesize(secondbyte,thirdbyte,fourthbyte)!=1) return 1;
	else return 0;
}


/* isheader() is not used any longer! I keep it for historical reasons. */
int isheader(int secondbyte, int thirdbyte, int fourthbyte)
{
/* it is very strict! */

	/* allowed:
	   111xx01x
	      00  0  226
	      00  1  227
	      10  0  242
	      10  1  243
	      11  0  250
	      11  1  251
	  not 01  0  234
	  not 01  1  235
	*/

	/* When we already know about some things: */
	if (fix_secondbyte!=0 && secondbyte!=fix_secondbyte) return 2;
	if (fix_sampfreq!=0 && sampfreq(secondbyte,thirdbyte)!=fix_sampfreq) return 3;
	if (fix_channelmode!=7 && channelmode(fourthbyte)!=fix_channelmode) return 4;

	/* second byte of mp3 header can only be 6 values  */
//	printf("\nsecondbyte=%u",secondbyte);
	if (secondbyte==226 || secondbyte==227 || secondbyte==242 || secondbyte==243 || secondbyte==250 || secondbyte==251)
	return 1;
	else
	return 0;
}


int bitrate(unsigned char secondbyte,unsigned char thirdbyte,unsigned char fourthbyte)
{
	unsigned char bitratenumber;

	bitratenumber = thirdbyte>>4;

	if(layer(secondbyte)==3) /* MPEG Layer3 */
	{
		if (mpeg(secondbyte)==1) /* MPEG1 */
		{
			switch (bitratenumber)
			{
				case 1: return 32;
				case 2: return 40;
				case 3: return 48;
				case 4: return 56;
				case 5: return 64;
				case 6: return 80;
				case 7: return 96;
				case 8: return 112;
				case 9: return 128;
				case 10: return 160;
				case 11: return 192;
				case 12: return 224;
				case 13: return 256;
				case 14: return 320;
			}
		}
		if (mpeg(secondbyte)==3 || mpeg(secondbyte)==2) /* MPEG2.5, MPEG2 */
		{
			switch (bitratenumber)
			{
				case 1: return 8;
				case 2: return 16;
				case 3: return 24;
				case 4: return 32;
				case 5: return 40;
				case 6: return 48;
				case 7: return 56;
				case 8: return 64;
				case 9: return 80;
				case 10: return 96;
				case 11: return 112;
				case 12: return 128;
				case 13: return 144;
				case 14: return 160;
			}
		}
	}

	if(layer(secondbyte)==2) /* MPEG Layer2 */
	{
		if (mpeg(secondbyte)==1) /* MPEG1 */
		{
			switch (bitratenumber)
			{
				case 1: return 32;
				case 2: return 48;
				case 3: return 56;
				case 4: return 64;
				case 5: return 80;
				case 6: return 96;
				case 7: return 112;
				case 8: return 128;
				case 9: return 160;
				case 10: return 192;
				case 11: return 224;
				case 12: return 256;
				case 13: return 320;
				case 14: return 384;
			}
		}
		if (mpeg(secondbyte)==3 || mpeg(secondbyte)==2) /* MPEG2.5, MPEG2 */
		{
			switch (bitratenumber)
			{
				case 1: return 8;
				case 2: return 16;
				case 3: return 24;
				case 4: return 32;
				case 5: return 40;
				case 6: return 48;
				case 7: return 56;
				case 8: return 64;
				case 9: return 80;
				case 10: return 96;
				case 11: return 112;
				case 12: return 128;
				case 13: return 144;
				case 14: return 160;
			}
		}
	}

	if(layer(secondbyte)==1) /* MPEG Layer1 */
	{
		if (mpeg(secondbyte)==1) /* MPEG1 */
		{
			switch (bitratenumber)
			{
				case 1: return 32;
				case 2: return 64;
				case 3: return 96;
				case 4: return 128;
				case 5: return 160;
				case 6: return 192;
				case 7: return 224;
				case 8: return 256;
				case 9: return 288;
				case 10: return 320;
				case 11: return 352;
				case 12: return 384;
				case 13: return 416;
				case 14: return 448;
			}
		}
	}

	/* In case of an error, bitrate=1 is returned */
	return 1;
}


int layer(unsigned char secondbyte)
{
	unsigned char layernumber;

	secondbyte = secondbyte<<5;
	layernumber = secondbyte>>6;
	switch (layernumber)
	{
		case 1: return 3;
		case 2: return 2;
// 		case 2: {printf("\nFile is an MP2, not am MP3.\n"); exitseq(99);}
		case 3: return 1;
	}
	/* In case of an error, layer=0 is returned */
	return 0;
}


int paddingbit(unsigned char thirdbyte)
{
	unsigned char temp;

	temp=thirdbyte<<6;
	return temp>>7;
}


int framesize(unsigned char secondbyte,unsigned char thirdbyte,unsigned char fourthbyte)
{
	int br=bitrate(secondbyte,thirdbyte,fourthbyte);
	int mfactor=2;
	int sf=sampfreq(secondbyte,thirdbyte);

	if (mpeg(secondbyte)!=1) mfactor=1;
	if ((br!=1) && (sf!=1)) return (mfactor*72000*br/sf)+paddingbit(thirdbyte);
	else return 1;
}

/* find next frame at seekpos */
long nextframe(long seekpos)
{
	long oldseekpos=seekpos;   /* remember seekpos */
	unsigned char a,b,c,d;

	if (seekpos>filesize) return filesize;

	fseek(mp3file, seekpos, SEEK_SET);
//	printf("seekpos=%li \n",seekpos);

	/* if seekpos is a header and framesize is valid, jump to next header via framesize, else move on one byte */
	a=fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);
	d=fgetc(mp3file);
	if (a==255 && framesize(b,c,d)!=1 && sampfreq(b,c)!=1) seekpos=seekpos+framesize(b,c,d);
	else seekpos=oldseekpos+1;

	/* find next possible header, start right at seekpos */
	fseek(mp3file, seekpos, SEEK_SET);

	while(1)           /* loop till break */
	{
		if (seekpos>=filesize) break;
		fseek(mp3file, seekpos, SEEK_SET);
		a=fgetc(mp3file);  /* get next byte */
		if (a==255)
		{
//			printf("seekpos=%li \n",seekpos);
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			if (framesize(b,c,d)!=1 && sampfreq(b,c)!=1) break;  /* next header found */
		}
		seekpos++;
//		printf("seekpos=%li \n",seekpos);
	}

//	printf("seekpos1=%li\n",seekpos);

	/* also check next frame if possible */
	if (seekpos+framesize(b,c,d)+4<filesize)
	{
		fseek(mp3file, seekpos+framesize(b,c,d), SEEK_SET);
		a=fgetc(mp3file);
		b=fgetc(mp3file);
		c=fgetc(mp3file);
		d=fgetc(mp3file);
		if (a==255 && framesize(b,c,d)!=1 && sampfreq(b,c)!=1);
		else seekpos=nextframe(seekpos+1);
	}

//	printf("seekpos3=%li\n",seekpos);
	return seekpos;
}


/* find previous frame at seekpos */
long prevframe(long seekpos)
{
	long oldseekpos=seekpos;   /* remember seekpos */
	long nextheaderpos;
	unsigned char a,b,c,d;

	if (seekpos<audiobegin) seekpos=audiobegin;

	/* find next header, start right at seekpos */
	nextheaderpos=nextframe(seekpos);

	/* rewind to previous header and check if nextheader is more than framesize bytes further */
	seekpos=oldseekpos-1;
	while(1)           /* loop till break */
	{
		if (seekpos<audiobegin) break;
		fseek(mp3file, seekpos, SEEK_SET);
		a=fgetc(mp3file);
		if (a==255)
		{
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			if (is_header(b,c,d) && seekpos+framesize(b,c,d)<=nextheaderpos && sampfreq(b,c)!=1) break;     /* previous frame found */
		}
		seekpos--;
//		printf("seekpos=%li \n",seekpos);
	}

	if (seekpos<audiobegin) seekpos=audiobegin;
	return seekpos;
}


void zaptitle (void)
{
	title[0]='\0';
}


int showmins (long bytes)
{
	if (bytes==-1) return totaltime/60000;
	else
	return bytes/avbr*8/60000;
}


double showsecs (long bytes)
{
	double temp;

	if (bytes==-1)
	{
		temp=totaltime/1000-showmins(-1)*60;
		if (temp>59.99) return 59.99; /* 59.997 would be displayed as 60:00 without this */
		return temp;
	}
	else
	temp=bytes;
	temp=(temp/avbr*8/1000-(showmins(bytes)*60));
	if (temp>59.99) return 59.99; /* 59.997 would be displayed as 60:00 without this */
	return temp;
}


/*
time of frame in milliseconds is: framesize*8/bitrate == 1152000/sampfreq !
because bitrate=128 means 128000bits/s
*/


long fforward (long seekpos,long skiptime)
{
	long double temptime=0;
	long double oldtotaltime=totaltime; /* remember totaltime */
	unsigned char a,b,c,d;
	long lastheaderpos=seekpos;
	int lastfrsize=0;
	int foundframe=0;

	if (exactmode==0) return nextframe(seekpos+msec*skiptime);

	printf("  seeking at        ");

	/* set lastfrsize: */
	fseek(mp3file, seekpos, SEEK_SET);
	a=fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);
	d=fgetc(mp3file);
	lastfrsize=framesize(b,c,d);

//	printf("  first temptime=%Lf   seekpos=%li     \n",temptime,seekpos);

	fseek(mp3file, seekpos, SEEK_SET);
	while(seekpos<=filesize)           /* loop till break */
	{
		if (temptime>=skiptime) break;
		if (seekpos>=filesize) break;
		seekpos++;
		if (foundframe)
		{
			seekpos=seekpos+lastfrsize-1;
			foundframe=0;
		}
		fseek(mp3file, seekpos, SEEK_SET);
		a=fgetc(mp3file);  /* get next byte */
		if (a==255)
		{
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			if (is_header(b,c,d) && seekpos>=lastheaderpos+lastfrsize && sampfreq(b,c)!=1)     /* next frame found */
			{
				temptime=temptime+fix_frametime; /* increase temptime for the first time*/
				foundframe=1;
				lastheaderpos=seekpos;
				lastfrsize=framesize(b,c,d);
				if (seekpos>filesize) break;     /* EOF reached? */
				totaltime=oldtotaltime+temptime; /* totaltime needs to be set for exact showmins() */
				printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(-1),showsecs(-1));
//				printf("  mid temptime=%Lf   seekpos=%li     \n",temptime,seekpos);
			}
		}
	}

	if (seekpos>lastheaderpos+lastfrsize) temptime=temptime+fix_frametime;

	if (seekpos>filesize)
	{
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b  End of file reached!     \n                                        ");
		seekpos=filesize;
	}

//	printf("last  temptime=%Lf   seekpos=%li     \n",temptime,seekpos);
	totaltime=oldtotaltime+temptime;
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	return seekpos;
}


long frewind (long seekpos,long skiptime)
{
	long double temptime=0;
	long double oldtotaltime=totaltime; /* remember totaltime */
	unsigned char a,b,c,d;
	long lastheaderpos;

	if (exactmode==0) return prevframe(seekpos-msec*skiptime);

	lastheaderpos=seekpos+1;
	printf("  seeking at        ");

//	printf("  first temptime=%Lf   seekpos=%li     \n",temptime,seekpos);

	fseek(mp3file, seekpos, SEEK_SET);
	while(1)                              /* loop till break */
	{
		if (temptime>skiptime) break;
		seekpos--;                       /* STEP ONE BYTE BACK */
		if (seekpos<=audiobegin)
		{
			seekpos=audiobegin;
			oldtotaltime=0;
			break;
		}

		fseek(mp3file, seekpos, SEEK_SET);
		a=fgetc(mp3file);
		if (a==255)
		{
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			if (is_header(b,c,d) && seekpos+framesize(b,c,d)<=lastheaderpos && sampfreq(b,c)!=1)     /* previous frame found */
			{
				lastheaderpos=seekpos;
//				printf("found header\n");
				totaltime=oldtotaltime-temptime;  /* totaltime needs to be set for exact showmins() */
				printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(-1),showsecs(-1));
				if (seekpos<=audiobegin) break;
				temptime=temptime+fix_frametime;                      /* sum up real time */
//				printf("  mid temptime=%Lf   seekpos=%li     \n",temptime,seekpos);
		if (temptime>skiptime) break;
			}
		}
	}

//	printf("last  temptime=%Lf   seekpos=%li     \n",temptime,seekpos);
	totaltime=oldtotaltime-temptime;
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	if (totaltime<0)
	{
		totaltime=0;
		seekpos=audiobegin;
	}
	return seekpos;
}


/* Determination of average VBR bitrate */
double avbitrate(void)
{
	double framecount=0, bitratesum=0;
	long int pos=audiobegin;
	unsigned char a,b,c,d;
	double thisfrsize=0, thisbitrate=0;

//	printf("  avbitrate");
	fseek(mp3file, audiobegin, SEEK_SET);
	while(1)                              /* loop till break */
	{
//		printf("\navbitrate in loop, pos=%lu, filesize=%lu\n",pos,filesize);
		if (pos>=filesize-1 || pos>SCANAREA) break;
		a=fgetc(mp3file); pos++;
//		printf("\na=%u\n",a);
		if (a==255)
		{
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			thisfrsize=framesize(b,c,d);                                     /* cache framesize(b,c,d) */
//			if (isheader(b,c,d)==1 && thisfrsize!=1 && sampfreq(b,c)!=1)     /* next frame found */
			if (thisfrsize!=1 && sampfreq(b,c)==fix_sampfreq)                /* next frame found */
			{
				fseek(mp3file, pos+thisfrsize-1, SEEK_SET);       /* skip framesize bytes */
				pos=pos+thisfrsize-1;                             /* adjust pos to actual SEEK_SET */
				framecount++;                                        /* one more frame */
				thisbitrate=bitrate(b,c,d);                          /* cache bitrate(b,c,d) */
				bitratesum=bitratesum+thisbitrate;
//				printf("\nbitratesum:%lu framecount:%lu ",bitratesum,framecount);
				if (bitratesum/framecount!=thisbitrate && vbr==0)    /* VBR check */
				{
					vbr=1;     /* if file is VBR */
//					printf("\nnoVBR: bitrate=%.0f at %lu Bytes \n",thisbitrate,pos);
				}
			}
			else fseek(mp3file, pos, SEEK_SET);    /* rewind to next byte for if (a==255) */
		}
	}

//	printf("\navbitrate()");

	if (bitratesum==0 || framecount==0) return 0;
	return bitratesum/framecount;
}


/* Determination of total number of frames */
long get_total_frames(void)
{
	long int pos=0, framecount=0;
	unsigned char a,b,c,d;
	int thisfrsize=0;
	long rememberpos=0;

	fseek(mp3file, 0, SEEK_SET);
	while(1)                              /* loop till break */
	{
		if (pos>=filesize-1) break;
		a=fgetc(mp3file); pos++;
//		printf("\na=%u\n",a);
		if (a==255)
		{
//			printf("a=255 at pos=%lu\n",pos);
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			thisfrsize=framesize(b,c,d);  /* cache framesize(b,c,d) */
			if (debug==10) printf(" pos=%lu  frsize=%u sampfreq=%u MPEG=%u\n",pos-1,thisfrsize,sampfreq(b,c),mpeg(b));
			if (thisfrsize!=1 && sampfreq(b,c)==fix_sampfreq)                /* next frame found */
			{
				if (debug==10) printf(" diff=%li pos=%li frsize=%u sampfreq=%u MPEG=%u \n",pos-1-rememberpos,pos-1,thisfrsize,sampfreq(b,c),mpeg(b));
				rememberpos=pos-1;
				fseek(mp3file, pos+thisfrsize-1, SEEK_SET);         /* skip framesize bytes */
				pos=pos+thisfrsize-1;                               /* adjust pos to actual SEEK_SET */
				framecount++;                                       /* one more frame */
			}
			else fseek(mp3file, pos, SEEK_SET);  /* go to next byte for if (a==255) */
		}
	}
	return framecount;
}


/* This function shall return the volume (energy level)
   of one frame. Taken from mpglib and mpcut. */
unsigned int volume(long playpos)
{
	unsigned char copy[BUFFER+5];
	long get_maxframe(FILE * zin);
	unsigned int level, framenum=4;
	int i, pos, frsize;

	FILE *volfile;
	char volname[15] = "/tmp/volXXXXXX";
	int fd=-1;

//	printf("  volume()");

	if (debug==1) printf("\nvolume(): before prevframe(playpos)");

	for (i=0; i<framenum ; i++) playpos=prevframe(playpos);  /* step back framenum frames */

	/* This should not change anything if the file is OK: */
	playpos=prevframe(playpos); playpos=nextframe(playpos);
	playpos=prevframe(playpos); playpos=nextframe(playpos);
	playpos=prevframe(playpos); playpos=nextframe(playpos);
	playpos=prevframe(playpos); playpos=nextframe(playpos);

	if (debug==1) printf("volume(): after %ux prevframe %li \n",framenum,playpos);

	/* Copy a small section into copy[] */
	fseek(mp3file, playpos, SEEK_SET);
//	printf("\n%lu        ",playpos);
	for (i=0 ; i<BUFFER && playpos+i<filesize ; i++)
	{
		copy[i]=fgetc(mp3file);
	}

	/* Copy framenum+2 frames to outfile */
	if ((fd = mkstemp(volname)) == -1 || (NULL== (volfile = fdopen(fd, "w+b"))) )
	{
		perror("\ncutmp3: failed writing temporary volume mp3 in /tmp");exitseq(6); /* open temp.vol.file read-writable zero length */
	}
	pos=0;
	for (framecount=0; framecount < framenum+2 ; framecount++)
	{
		/* seek next frame */
		while (
		        (   (frsize=framesize(copy[pos+1],copy[pos+2],copy[pos+3]))==1
		          || sampfreq(copy[pos+1],copy[pos+2])!=fix_sampfreq
		          || bitrate(copy[pos+1],copy[pos+2],copy[pos+3])==1
//		          || isheader(copy[pos+1],copy[pos+2],copy[pos+3])!=1
		        ) && pos < BUFFER
		      ) pos++; /* while not header and still inside copy[], move on */
//		printf("pos=%u  frsize=%u  sampfreq=%u\n",pos,frsize,sampfreq(copy[pos+1],copy[pos+2]));
		/* write this frame */
		for (i=0 ; i<frsize && pos<BUFFER && playpos+pos<filesize ; i++)
		{
			fputc(copy[pos],volfile);
			pos++;
		}
	}

	/* get energy level */
	fseek(volfile, 0, SEEK_SET);
	if (debug==1) printf("volume(): before InitMP3() \n");
	InitMP3();
	if (debug==1) printf("volume(): before get_framelevel \n");
	level = get_framelevel(volfile,framenum);
	if (debug==1) printf("volume(): after get_framelevel \n");
	if (level==MP3_ERR) level=65535;
	if (level<0) level=(-1)*level;
	if (level>65535) level=65535;
	ExitMP3();
	fclose(volfile);
	remove(volname);
	return level;
}


/* This function returns the position for the next silence */
long seeksilence(long seekpos)
{
	unsigned int vol;
	long silstart, silmid;
	long cached;

	if (exactmode==1)
	{
		exactmode=0;
		printf("  WARNING: silence seeking switches off exact mode.\n                    ");
	}
//	printf("  seeksilence()");

	seekpos=nextframe(seekpos+msec*silencelength); /* move on */
	if (seekpos>=dataend) /* dataend reached? */
	{
		printf("  seeking at        ");
		return filesize;
	}

	printf("  seeking at        ");
	while (seekpos<dataend)
	{
		while ((vol=volume(seekpos)) >= silvol)
		{
			/* look for possible silence every silencelength milliseconds: */
			if (debug==9) printf("\ncandidate seek at %lu   volume is %u\n",seekpos,vol);
			printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
			seekpos=nextframe(seekpos+msec*silencelength);
			if (seekpos>=dataend) return filesize; /* dataend reached? */
		}

		/* we found a candidate! */
		if (debug==9) printf("\ncandidate found at %lu   volume is %u.\n",seekpos,vol);
		printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
		silmid=seekpos; /* remember first found silent frame */
		seekpos=nextframe(seekpos); /* move on to prevent endless loop */

		/* rewind to first silent frame if necessary */
		while ((vol=volume(seekpos)) < silvol)
		{
			cached=prevframe(seekpos);
			if (seekpos==cached) break;
			seekpos=cached;
			if (debug==9) printf("\nrewind seek at %lu   volume is %u\n",seekpos,vol);
			if (debug==9) printf("\naudiobegin is %u",audiobegin);
			if ((silmid-seekpos) >= silencelength*msec) break; /* silence already long enough */
			if (seekpos<=audiobegin) break;
		}

		silstart=seekpos; /* mark start of silence */
		seekpos=silmid; /* go back to first found silent frame */
		if (debug==9) printf("\nsilmid at %lu   volume is %u\n",seekpos,vol);

		/* quick check: if volume at silstart+silencelength is too loud, length of silence is too short */
		if (volume(silstart+msec*silencelength) >= silvol) seekpos=seekpos+msec*silencelength;
		else /* scan length of silence */
		{
			/* Now we have the first quiet frame, do a length inquiry for silence
			This is not very fast... */
			while ((vol=volume(seekpos)) < silvol)
			{
				if (debug==9) printf("\nsilstart is %lu   seek at nextframe: %lu   volume is %u                ",silstart,nextframe(seekpos),vol);
				printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
//				if ((seekpos=nextframe(seekpos)) == nextframe(seekpos)) return filesize; /* move on and EOF reached? */
				seekpos=nextframe(seekpos); /* move on */
				if (seekpos+msec*silencelength>=dataend) return filesize; /* distance to dataend shorter than minimum silencelength? */
				if ((seekpos-silstart) >= silencelength*msec*silfactor && silfactor!=0) /* silence much longer than wanted */
				{
					printf("\n\nSilence is already %lux longer than wanted!\nPress p to search on.                    ",silfactor);
					return seekpos;
				}
			}
			if ((seekpos-silstart) >= silencelength*msec) /* silence long enough? */
			{
				return nextframe(seekpos-(msec/2*silencelength)); /* short silence in the beginning */
			}
		}
	}
	return filesize; /* EOF */
}


/* This function returns the position of the beginning of the next silence */
long seeksilstart(long seekpos)
{
	unsigned int vol;
	long silstart;
	long startpoint=seekpos;
	long cached;

	if (exactmode==1)
	{
		exactmode=0;
		printf("  WARNING: silence seeking switches off exact mode.\n                    ");
	}
//	printf("  seeksilence()");

	seekpos=nextframe(seekpos+msec*silencelength); /* move on */
	if (seekpos>=dataend) /* dataend reached? */
	{
		printf("  seeking at        ");
		return filesize;
	}

	printf("  seeking at        ");
	while (seekpos<dataend)
	{
		while ((vol=volume(seekpos)) >= silvol)
		{
			/* look for possible silence every silencelength milliseconds: */
			if (debug==9) printf("\ncandidate seek at %lu   volume is %u\n",seekpos,vol);
			printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
			seekpos=nextframe(seekpos+msec*silencelength);
			if (seekpos>=dataend) return filesize; /* dataend reached? */
		}

		/* we found a possible silence! */
		if (debug==9) printf("\ncandidate found at %lu   volume is %u\n",seekpos,vol);
		printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
		seekpos=nextframe(seekpos); /* move on to prevent endless loop */

		/* rewind to first silent frame */
		while ((vol=volume(seekpos)) < silvol)
		{
			cached=prevframe(seekpos);
			if (seekpos==cached) break;
			seekpos=cached;
			if (debug==9) printf("\nrewind seek at %lu   volume is %u\n",seekpos,vol);
			if (debug==9) printf("\naudiobegin is %u",audiobegin);
			if (seekpos<=audiobegin) break; /* at beginning of file? */
		}

		seekpos=nextframe(seekpos); /* move on because actual frame is too loud */

		silstart=seekpos; /* mark start of silence */

		/* Do not find the same silence again! Scan to end of silence and call seeksilstart() again */
		if (startpoint==nextframe(silstart+(msec/2*silencelength))) /* if input value==return value */
		{
			while ((vol=volume(seekpos)) < silvol)
			{
				printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
				seekpos=nextframe(seekpos); /* move on */
				if (seekpos+msec*silencelength>=dataend) return filesize; /* distance to EOF shorter than minimum silencelength? */
			}
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			return seeksilstart(nextframe(seekpos));
		}

		if (debug==9) printf("\nsilstart at %lu   volume is %u\n",seekpos,volume(seekpos));

		/* quick check: if volume at silstart+silencelength is too loud, length of silence is too short */
		if (volume(silstart+msec*silencelength) >= silvol) seekpos=seekpos+msec*silencelength;
		else /* scan length of silence */
		{
			/* Now we have the first quiet frame, do a length inquiry for silence
			This is not very fast... */
			while ((vol=volume(seekpos)) < silvol && seekpos-silstart < silencelength*msec) /* don't seek further than silencelength here */
			{
				if (debug==9) printf("\nsilstart is %lu   seek at nextframe: %lu   volume is %u                ",silstart,nextframe(seekpos),vol);
				printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
//				if (nextframe(seekpos) >= dataend) return filesize; /* dataend reached? */
				seekpos=nextframe(seekpos); /* move on */
				if (seekpos+msec*silencelength>=dataend) return filesize; /* distance to dataend shorter than minimum silencelength? */
			}
			if ((seekpos-silstart) >= silencelength*msec) /* silence long enough? */
			{
				return nextframe(silstart+(msec/2*silencelength)); /* return end of noise plus short silence */
			}
		}
	}
	return filesize; /* EOF */
}


/* This function prints the reached ID3 V1 TAG */
void showtag(long seekpos)
{
	if (importtag(seekpos)==0) return;

	printf("\n\nReached end of this track:   ");
	printf("\nTitle:   %s",title);
	printf("\nArtist:  %s",artist);
	printf("\nAlbum:   %s",album);
	printf("\nYear:    %s",year);
	printf("\nComment: %s",comment);
	printf("\n\n[1..0,r,a,b,s,q] >                      ");
	return;
}


/*
ID3 V1 Implementation

   The Information is stored in the last 128 bytes of an MP3. The Tag
   has got the following fields, and the offsets given here, are from
   0-127.

     Field      Length    Offsets
     Tag        3           0-2
     Songname   30          3-32
     Artist     30         33-62
     Album      30         63-92
     Year       4          93-96
     Comment    30         97-126
     Genre      1           127

   The string-fields contain ASCII-data, coded in ISO-Latin 1 codepage.
   Strings which are smaller than the field length are padded with zero-
   bytes.

     Tag: The tag is valid if this field contains the string "TAG". This
        has to be uppercase!

     Songname: This field contains the title of the MP3 (string as
        above).

     Artist: This field contains the artist of the MP3 (string as above).

     Album: this field contains the album where the MP3 comes from
        (string as above).

     Year: this field contains the year when this song has originally
        been released (string as above).

     Comment: this field contains a comment for the MP3 (string as
        above). In version 1.1 Offset 126 is binarily used for the track number.

     Genre: this byte contains the offset of a genre in a predefined
        list the byte is treated as an unsigned byte. The offset is
        starting from 0.
*/

/* This function gets data from ID3 V2 and returns its length
   Argument is start of tag */
long importid3(long seekpos)
{
	long int length=0, pos=0, i=0;
	int size=0, hasexthdr=0, footer=0;
	unsigned char a,b,c,d;
	int majorv, minorv;

//	printf("\nimportid3 at %li\n",seekpos);

	fseek(mp3file, seekpos, SEEK_SET);
	length=skipid3v2(seekpos)-seekpos; /* also checks if there is ID3 V2 */
	if (length==0) return 0; /* is not ID3 V2 */

	copyid3(seekpos, seekpos+length); /* copy data to file */

	/* delete old stuff in case they are not overwritten: */
	title[0]='\0';
	year[0]='\0';
	album[0]='\0';
	artist[0]='\0';
	comment[0]='\0';

	fseek(id3file2, 0, SEEK_SET);
	for (i=0 ; i < 3 ; i++) {fgetc(id3file2); pos++;} /* skip ID3 */
	majorv=fgetc(id3file2); pos++;
	minorv=fgetc(id3file2); pos++;
	a=b=fgetc(id3file2); pos++; /* flags byte */
//	printf(" flags byte=%u ",a);
	if(majorv!=2) return 0; /* only V2 */
	if(minorv>=3) hasexthdr=(a<<1)>>7; /* has extended header? */
	if(minorv==4) footer=((b<<3)>>7)*10; /* has footer? */
	for (i=0 ; i < 4 ; i++) {fgetc(id3file2); pos++;} /* skip tag length info */
	if (hasexthdr)
	{
		/* skip ext header */
		a=fgetc(id3file2);
		b=fgetc(id3file2);
		c=fgetc(id3file2);
		d=fgetc(id3file2);
		pos=pos+4;
		size=a*128*128*128+b*128*128+c*128+d;
		for (i=0 ; i < size-4 ; i++) {fgetc(id3file2); pos++;}
//		printf("\nhasexthdr of size=%i\n",size);
	}

	if (minorv==3 || minorv==4) /* ID3V2.3 & ID3V2.4 */
	{
		while (pos<length)
		{
	//		printf("\nloopstart pos=%li length=%li footer=%i\n",pos,length,footer);
			if (pos+4>=length-footer) break;
			a=fgetc(id3file2);
			b=fgetc(id3file2);
			c=fgetc(id3file2);
			d=fgetc(id3file2);
			pos=pos+4;
	//		printf("\nloopstart: a=%c b=%c c=%c d=%c\n",a,b,c,d);

			if (a==0 && b==0 && c==0 && d==0) break; /* is padding at the end of the tag */
			else if (a=='T' && b=='I' && c=='T' && d=='2') /* read title */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
				fgetc(id3file2);
				fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(title,size+1,id3file2);
				else
				{
					fgets(title,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
	//			printf("Title: %s\n",title);
			}
			else if (a=='T' && b=='P' && c=='E' && d=='1') /* read artist */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
				fgetc(id3file2);
				fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(artist,size+1,id3file2);
				else
				{
					fgets(artist,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
	//			printf("Artist: %s\n",artist);
			}
			else if (a=='T' && b=='Y' && c=='E' && d=='R') /* read year */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
				fgetc(id3file2);
				fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(year,size+1,id3file2);
				else
				{
					fgets(year,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
	//			printf("Year: %s\n",year);
			}
			else if (a=='T' && b=='A' && c=='L' && d=='B') /* read album */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
				fgetc(id3file2);
				fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(album,size+1,id3file2);
				else
				{
					fgets(album,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
	//			printf("Album: %s\n",album);
			}
			else if (a=='C' && b=='O' && c=='M' && d=='M') /* read comment */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
				fgetc(id3file2);
				fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(comment,size+1,id3file2);
				else
				{
					fgets(comment,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
	//			printf("Comment: %s\n",comment);
			}
			else
			{
				/* frame not important, so skip it */
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
	//			printf("\nskip a=%u b=%u c=%u d=%u\n",a,b,c,d);
				fgetc(id3file2);
				fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
	//			printf("\nskip size=%li length=%li\n",size,length);
				if (size>length-pos) break;
				for (i=0 ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				pos=pos+size;
			}
		}
	}

	if (minorv==0) /* ID3V2.0 */
	{
		while (pos<length)
		{
			if (pos+3>=length) break;
			a=fgetc(id3file2);
			b=fgetc(id3file2);
			c=fgetc(id3file2);
			pos=pos+3;

			if (a==0 && b==0 && c==0) break; /* is padding at the end of the tag */
			else if (a=='T' && b=='T' && c=='2') /* read title */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(title,size+1,id3file2);
				else
				{
					fgets(title,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
			}
			else if (a=='T' && b=='P' && c=='1') /* read artist */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(artist,size+1,id3file2);
				else
				{
					fgets(artist,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
			}
			else if (a=='T' && b=='Y' && c=='E') /* read year */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(year,size+1,id3file2);
				else
				{
					fgets(year,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
			}
			else if (a=='C' && b=='O' && c=='M') /* read comment */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(comment,size+1,id3file2);
				else
				{
					fgets(comment,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
			}
			else if (a=='T' && b=='A' && c=='L') /* read album */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(album,size+1,id3file2);
				else
				{
					fgets(album,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
			}
			else
			{
				/* frame not important, so skip it */
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (size>length-pos) break;
				for (i=0 ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				pos=pos+size;
			}
		}
	}

	/* in case there was no info about them: */
	if (strlen(title)==0) snprintf(title,31,"%s","unknown title");
	if (strlen(artist)==0) snprintf(artist,31,"%s","unknown artist");

	return length;

	/*
	Main info in V2.4:

	Structure:
	header 10 bytes
	optional ext. header
	frames
	optional padding (var. length) or footer (10 bytes)

	Frames:
	TIT2=title
	TPE1=artist
	TYER=year
	COMM=comment
	TALB=album
	*/

	/*
	Main info in V2.0:

	Structure:
	header 10 bytes
	frames

	Frames:
	ABCXXX with XXX=size, ABC as follows:

	TT2=title
	TP1=artist
	TYE=year
	COM=comment
	TAL=album
	*/
}


/* This function is only used for file info */
void infotag(long seekpos)
{
	if (importtag(seekpos)==0) return;

	printf("\nID3 V1 tag:");
	printf("\nTitle:   %s",title);
	printf("\nArtist:  %s",artist);
	printf("\nAlbum:   %s",album);
	printf("\nYear:    %s",year);
	printf("\nComment: %s\n",comment);
	return;
}


/* This function copies the ID3 V1 tag to a tempfile */
void copytag(long seekpos)
{
	int j=0;

	if (seekpos < 0) seekpos=0;
	if (seekpos > filesize) seekpos=filesize;

	fseek(mp3file, seekpos-128, SEEK_SET); /* set to right position */
	if (fgetc(mp3file)!='T' || fgetc(mp3file)!='A' || fgetc(mp3file)!='G' )
	return; /* if TAG is not there */

	/* copy tag to file */
	fseek(mp3file, seekpos-128, SEEK_SET);
	fseek(id3file1, 0, SEEK_SET);
	for (j=0 ; j < 128 ; j++) fputc(getc(mp3file),id3file1);

	return;
}


/* This function gets data from ID3 V1 and returns its length
   Argument is end of tag */
int importtag(long seekpos)
{
	int j=0;

//	printf("\nimporttag at %li\n",seekpos);

	if (seekpos < 0) seekpos=0;
	if (seekpos > filesize) seekpos=filesize;

	fseek(mp3file, seekpos-128, SEEK_SET); /* set to right position */
	if (fgetc(mp3file)!='T' || fgetc(mp3file)!='A' || fgetc(mp3file)!='G' )
	return 0; /* if TAG is not there */

	fgets(title,31,mp3file); /* save title */
	for (j=29; title[j]==' ' ; j--) title[j]='\0'; /* erase trailing spaces */
	fgets(artist,31,mp3file); /* save artist */
	for (j=29; artist[j]==' ' ; j--) artist[j]='\0'; /* erase trailing spaces */
	fgets(album,31,mp3file); /* save album */
	for (j=29; album[j]==' ' ; j--) album[j]='\0'; /* erase trailing spaces */
	fgets(year,5,mp3file); /* save year */
	fgets(comment,31,mp3file); /* save comment */
	for (j=29; comment[j]==' ' ; j--) comment[j]='\0'; /* erase trailing spaces */
	genre=fgetc(mp3file);

	copytag(seekpos);

	/* in case there was no info about them: */
	if (strlen(title)==0) snprintf(title,31,"%s","unknown title");
	if (strlen(artist)==0) snprintf(artist,31,"%s","unknown artist");

	return 128;
}


/* This function skips ID3 V2 tag, if any.
   Useful also for getting the length of the ID3 V2 tag.
   Returns its length. */
long skipid3v2(long seekpos)
{
	long oldseekpos=seekpos;   /* remember seekpos */
	unsigned char buffer[15];
	int pos, footer;

	if (seekpos+10>filesize) return seekpos;

	fseek(mp3file, seekpos, SEEK_SET);

	/* Load 10 Bytes into unsigned buffer[] */
	for (pos=0 ; pos<10 && pos<=filesize ; pos++) buffer[pos]=fgetc(mp3file);

	/* check for ID3 V2, this is not safe, but it cannot be done better! */
	if (buffer[0]=='I' && buffer[1]=='D' && buffer[2]=='3' && buffer[3]<5 && buffer[4]<10 && (buffer[5]<<4)==0 && buffer[6]<128 && buffer[7]<128 && buffer[8]<128 && buffer[9]<128)
	{
//		printf("\nID3 V2 found at %Li\n",seekpos);
		footer=buffer[5]; footer=footer<<3; footer=footer>>7; footer=footer*10; /* check for footer presence */
		seekpos=seekpos+10+buffer[6]*128*128*128+buffer[7]*128*128+buffer[8]*128+buffer[9]+footer;
	}
	fseek(mp3file, oldseekpos, SEEK_SET); /* really necessary? */
	return seekpos;

	/*
	buffer[3] = v2 minor version (2.X.0)
	buffer[4] = v2 revision number (2.4.X)
	buffer[5] = flags byte, 4 lowest bits must be zero
	buffer[6-9] = synchsafe, highest bit is zero
	*/
}


/* This function prints the reached ID3 V2 TAG */
long showid3(long showpos)
{
	printf("\n\nFound ID3 V2 tag of this next track:");
	importid3(showpos);
	printf("\nTitle:   %s",title);
	printf("\nArtist:  %s",artist);
	printf("\nAlbum:   %s",album);
	printf("\nYear:    %s",year);
	printf("\nComment: %s\n",comment);
	printf("\n[1..0,r,a,b,s,q] >                      ");
	return 0;
}


/* This function prints the reached ID3 V2 TAG */
long alsoid3(long showpos)
{
	printf("\n\nFound ID3 V2 tag of this next track:");
	importid3(showpos);
	printf("\nTitle:   %s",title);
	printf("\nArtist:  %s",artist);
	printf("\nAlbum:   %s",album);
	printf("\nYear:    %s",year);
	printf("\nComment: %s",comment);
	return 0;
}


/* This function copies the ID3 V2 tag to a tempfile */
void copyid3(long startpos, long endpos)
{
// 	char id3name2[8191]="/tmp/id3v2tag";
	long bytesin=0;
// 	FILE *id3file2;

	long int length=0, pos=0, i=0;
// 	char id3name[32]="/tmp/id3v2tag";
	int size=0, hasexthdr=0, footer=0;
// 	FILE *id3file2;
	unsigned char a,b,c,d;
	int majorv, minorv;

	if (startpos < 0) startpos=0;
	if (endpos > filesize) endpos=filesize;
	if (endpos < startpos) return;

	fseek(mp3file, startpos, SEEK_SET);
	fseek(id3file2, 0, SEEK_SET);
	for (bytesin=0 ; bytesin < endpos-startpos ; bytesin++)
	{
		fputc(getc(mp3file),id3file2);
	}
// 	fclose(id3file2);

// 	id3file2 = fopen(id3name,"rb");
	fseek(id3file2, 0, SEEK_SET);
	for (i=0 ; i < 3 ; i++) {fgetc(id3file2); pos++;} /* skip ID3 */
	majorv=fgetc(id3file2); pos++;
	minorv=fgetc(id3file2); pos++;
	a=b=fgetc(id3file2); pos++; /* flags byte */
//	printf(" flags byte=%u ",a);
// 	if(majorv!=2) {fclose(id3file2); return;} /* only V2 */
	if(majorv!=2) {return;} /* only V2 */
	if(minorv>=3) hasexthdr=(a<<1)>>7; /* has extended header? */
	if(minorv==4) footer=((b<<3)>>7)*10; /* has footer? */
	for (i=0 ; i < 4 ; i++) {fgetc(id3file2); pos++;} /* skip tag length info */
	if (hasexthdr)
	{
		/* skip ext header */
		a=fgetc(id3file2);
		b=fgetc(id3file2);
		c=fgetc(id3file2);
		d=fgetc(id3file2);
		pos=pos+4;
		size=a*128*128*128+b*128*128+c*128+d;
		for (i=0 ; i < size-4 ; i++) {fgetc(id3file2); pos++;}
//		printf("\nhasexthdr of size=%i\n",size);
	}

	if (minorv==3 || minorv==4) /* ID3V2.3 & ID3V2.4 */
	{
		while (pos<length)
		{
	//		printf("\nloopstart pos=%li length=%li footer=%i\n",pos,length,footer);
			if (pos+4>=length-footer) break;
			a=fgetc(id3file2);
			b=fgetc(id3file2);
			c=fgetc(id3file2);
			d=fgetc(id3file2);
			pos=pos+4;
	//		printf("\nloopstart: a=%c b=%c c=%c d=%c\n",a,b,c,d);

			if (a==0 && b==0 && c==0 && d==0) break; /* is padding at the end of the tag */
			else if (a=='T' && b=='L' && c=='E' && d=='N') /* track length info */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
				fgetc(id3file2); fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(title,size+1,id3file2);
				else
				{
					fgets(title,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
//				printf("Track length: %s\n",title);
			}
			else
			{
				/* frame not important, so skip it */
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				d=fgetc(id3file2);
//				printf("\nskip a=%u b=%u c=%u d=%u\n",a,b,c,d);
				fgetc(id3file2); fgetc(id3file2); /* skip 2 flag bytes */
				pos=pos+6;
				size=a*128*128*128+b*128*128+c*128+d;
//				printf("\nskip size=%li length=%li\n",size,length);
				if (size>length-pos) break;
				for (i=0 ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				pos=pos+size;
			}
		}
	}

	if (minorv==0) /* ID3V2.0 */
	{
		while (pos<length)
		{
			if (pos+3>=length) break;
			a=fgetc(id3file2);
			b=fgetc(id3file2);
			c=fgetc(id3file2);
			pos=pos+3;

			if (a==0 && b==0 && c==0) break; /* is padding at the end of the tag */
			else if (a=='T' && b=='L' && c=='E') /* track length */
			{
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (fgetc(id3file2)==0) {size=size-1;pos++;} else fseek(id3file2, pos, SEEK_SET); /* Possible unsynch byte */
				if (size<250) fgets(title,size+1,id3file2);
				else
				{
					fgets(title,251,id3file2);
					for ( ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				}
				pos=pos+size;
			}
			else
			{
				/* frame not important, so skip it */
				a=fgetc(id3file2);
				b=fgetc(id3file2);
				c=fgetc(id3file2);
				pos=pos+3;
				size=a*128*128+b*128+c;
				if (size>length-pos) break;
				for (i=0 ; i<size ; i++) {fgetc(id3file2);} /* skip the rest if any */
				pos=pos+size;
			}
		}
	}

	return;
}


/* This function returns the position of the next ID3 V1/V2 TAG */
long seektag(long seekpos)
{
	unsigned char a,b,c,tag=0;
	long skippedpos, id3pos;

	if (exactmode==1)
	{
		exactmode=0;
		printf("  WARNING: tag seeking switches off exact mode.\n                    ");
	}

//	printf("%lu\n",seekpos);
	printf("  seeking at        ");

	seekpos++; /* do not find ID3 again */

	while (seekpos<filesize)
	{
		fseek(mp3file, seekpos, SEEK_SET); /* (re)set to right position */
		while ((a=fgetc(mp3file))!='T' && a!='I' && seekpos<filesize) seekpos++; /* look for 'T' or 'I' */
		seekpos++;
		printf("\b\b\b\b\b\b\b%4u:%02.0f",showmins(seekpos-audiobegin),showsecs(seekpos-audiobegin));
		b=fgetc(mp3file);
		c=fgetc(mp3file);
		if (a=='T' && b=='A' && c=='G' ) /* TAG is there */
		{
			if (nextframe(seekpos) == seekpos+127) tag=1; /* TAG is valid, next frame is OK */
			if (skipid3v2(seekpos+127) != seekpos+127) tag=2; /* TAG is valid, ID3 is next */
			if (tag>0)
			{
				/* skip TAG and return position */
				if (tag==2) alsoid3(seekpos+127); /* Also show next track info */
				showtag(seekpos+127);
				return seekpos+127;
			}
		}
		if (a=='I' && b=='D' && c=='3' ) /* ID3 is there */
		{
			id3pos=seekpos-1;
			if ( (skippedpos=skipid3v2(seekpos-1)) != seekpos-1 )  /* ID3 is valid */
			{
				tag=1;
				fseek(mp3file, skippedpos, SEEK_SET); /* Now check if TAG is next */
				a=fgetc(mp3file);
				b=fgetc(mp3file);
				c=fgetc(mp3file);
				if (a=='T' && b=='A' && c=='G' ) /* ID3 was an appended tag, because TAG is next. */
				{
					showtag(skippedpos+128);
					/* Skip TAG and return position */
					return seekpos+128;
				}
				else /* ID3 is a prepended tag */
				{
					showid3(id3pos);
					return seekpos-1; /* return position of ID3 */
				}
			}
		}
	}
	return filesize; /* no more tags */
}


/* This function writes the configuration file */
void writeconf(void)
{
	FILE *conffile;
	char filename1[8191];

	snprintf(filename1,8190,"%s/%s",getenv("HOME"),".cutmp3rc");
	/* check for conffile */
	if (NULL== (conffile = fopen(filename1,"wb")))
	{
		printf("  configuration file could NOT be written!     \n");
		return;
	}

	fprintf(conffile, "leng=");
	fprintf(conffile, "%u",silencelength);
	fprintf(conffile, "\n");
	fprintf(conffile, "volu=");
	fprintf(conffile, "%u",silvol);
	fprintf(conffile, "\n");
	fprintf(conffile, "card=");
	fprintf(conffile, "%u",card);
	fprintf(conffile, "\n");
	fprintf(conffile, "mute=");
	fprintf(conffile, "%u",mute);
	fprintf(conffile, "\n");
	fclose(conffile);
	printf("\n\n     length of silence is %u milliseconds",silencelength);
	printf("\n     maximum volume during silence is %u ",silvol);
	printf("\n     using soundcard #%u ",card);
	printf("\n     sound is ");
	if (mute) printf("OFF \n");
	else printf("ON \n");
	printf("\n     This configuration has been saved.     \n");
	return;
}


/* This function plays sound at the current position */
void playsel(long int playpos)
{
	long bytesin;
	long playsize=avbr*128*howlong; /* approx. 'howlong' seconds */
	long pos;
	long temppos=0;
	FILE *outfile;
	char command[1023];
	char playname[16] = "/tmp/playXXXXXX";
	int fd=-1;
	int i=0;

	if (!mute)
	{
		if (playpos < audiobegin)
		{
			playsize=playsize-(audiobegin-playpos);
			playpos=audiobegin;
		}
		if (playpos > filesize) playpos=filesize;
		if ((fd = mkstemp(playname)) == -1 || (NULL== (outfile = fdopen(fd, "w+b"))) )
		{
			perror("\ncutmp3: failed writing temporary mp3 in /tmp");exitseq(9);
		}
		fseek(mp3file, playpos, SEEK_SET);
		for (bytesin=0; bytesin < playsize; bytesin++) fputc(getc(mp3file),outfile);
		fclose(outfile);
	}

	if (exactmode==0) pos=playpos;
	else pos=audiobegin-1;

	if (!mute)
	{
 		if (card > 1)
		snprintf(command,1022," %s %s %s%u %s %s %s","mpg123",playname,"-a /dev/dsp",card-1,">",logname,"2>&1 &");
// 		else if (card==3)
// 		snprintf(command,1022," %s %s %s","mpg123",playname,"-a /dev/dsp2 > /tmp/mpg123.log 2>&1 &");
// 		else if (card==4)
// 		snprintf(command,1022," %s %s %s","mpg123",playname,"-a /dev/dsp3 > /tmp/mpg123.log 2>&1 &");
// 		else if (card==5)
// 		snprintf(command,1022," %s %s %s","mpg123",playname,"-a /dev/dsp4 > /tmp/mpg123.log 2>&1 &");
		else
		snprintf(command,1022," %s %s %s %s %s","mpg123",playname,">",logname,"2>&1 &");
		system(command);  /* play mp3 excerpt */

// 		printf("%s\n",command); /* debug */

		printf("  playing at %3u:%05.2f",showmins(pos-audiobegin),showsecs(pos-audiobegin));

		/* show live time? */
		if (livetime==1)
		{
			while (i<howlong*20 && temppos < filesize)
			{
				temppos=pos-audiobegin+i*avbr*6.35;
				printf("\b\b\b\b\b\b\b\b\b\b %3u:%05.2f",showmins(temppos),showsecs(temppos));
				usleep(50000);
				i++;
			}
		}

		if (playpos >= inpoint)
		printf(" / ~%u:%02.0f  (~%u:%02.0f after startpoint) \n",totalmins,totalsecs,showmins(playpos-inpoint),showsecs(playpos-inpoint));
		else
		printf(" / ~%u:%02.0f  (~%u:%02.0f before startpoint) \n",totalmins,totalsecs,showmins(inpoint-playpos),showsecs(inpoint-playpos));

	}
	else
	{
		if (playpos >= inpoint)
		printf("  position is %3u:%05.2f / ~%u:%02.0f  (~%u:%02.0f after startpoint) \n",showmins(pos-audiobegin),showsecs(pos-audiobegin),totalmins,totalsecs,showmins(playpos-inpoint),showsecs(playpos-inpoint));
		else
		printf("  position is %3u:%05.2f / ~%u:%02.0f  (~%u:%02.0f before startpoint) \n",showmins(pos-audiobegin),showsecs(pos-audiobegin),totalmins,totalsecs,showmins(inpoint-playpos),showsecs(inpoint-playpos));
	}

	if (!mute && livetime==0) { usleep(50000); } /* wait some time to start playing the file */
 	remove(playname); /* delete tempfile */

}


/* This function plays sound up to the current position */
void playtoend(long int playpos)
{
	long pos;

	if (exactmode==0) pos=playpos;
	else pos=audiobegin-1;

	if (mute)
	{
		if (playpos >= inpoint)
		printf("  position is %3u:%05.2f / ~%u:%02.0f  (~%u:%02.0f after startpoint) \n",showmins(pos-audiobegin),showsecs(pos-audiobegin),totalmins,totalsecs,showmins(playpos-inpoint),showsecs(playpos-inpoint));
		else
		printf("  position is %3u:%05.2f / ~%u:%02.0f  (~%u:%02.0f before startpoint) \n",showmins(pos-audiobegin),showsecs(pos-audiobegin),totalmins,totalsecs,showmins(inpoint-playpos),showsecs(inpoint-playpos));
	}
	else
	{
		printf("\n  Now playing up to location. ");
		if (playpos-avbr*128*howlong < audiobegin) playsel(playpos-avbr*128*howlong);
		else playsel(prevframe(playpos-avbr*128*howlong));
	}
	return;
}


/* This function reads the configuration file */
void readconf(void)
{
	FILE *conffile;
	long confsize=0,pos=0;
	unsigned char conf[BUFFER];
	char filename1[8191];

	snprintf(filename1,8190,"%s/%s",getenv("HOME"),".cutmp3rc");      /* get name of conffile */
	if (NULL == (conffile = fopen(filename1,"rb"))) return;      /* check for conffile */
	fseek(conffile, 0, SEEK_END);
	confsize=ftell(conffile);         /* size of conffile */

	/* read conffile into buffer: */
	fseek(conffile, 0, SEEK_SET);
	for (pos=0 ; pos<BUFFER && pos<confsize ; pos++) conf[pos]=fgetc(conffile);
	fclose(conffile);

	pos=0;        /* reset pos */
	while(1)                              /* loop till break */
	{
		if (pos==confsize || pos==BUFFER) break;
		if (conf[pos]=='l')
		{
			pos++;
			if (conf[pos]=='e' && conf[pos+1]=='n' && conf[pos+2]=='g' && conf[pos+3]=='=')
			{
				pos=pos+4;
				silencelength=0;
				while(isdigit(conf[pos]))
				{
					silencelength = silencelength*10 + conf[pos]-48;
					pos++;
				}
			}
		}
		if (conf[pos]=='v')
		{
			pos++;
			if (conf[pos]=='o' && conf[pos+1]=='l' && conf[pos+2]=='u' && conf[pos+3]=='=')
			{
				pos=pos+4;
				silvol=0;
				while(isdigit(conf[pos]))
				{
					silvol = silvol*10 + conf[pos]-48;
					pos++;
				}
			}
		}
		if (conf[pos]=='c')
		{
			pos++;
			if (conf[pos]=='a' && conf[pos+1]=='r' && conf[pos+2]=='d' && conf[pos+3]=='=')
			{
				pos=pos+4;
				if (isdigit(conf[pos]))
				{
					card = conf[pos]-48;
					pos++;
				}
			}
		}
		if (conf[pos]=='m')
		{
			pos++;
			if (conf[pos]=='u' && conf[pos+1]=='t' && conf[pos+2]=='e' && conf[pos+3]=='=')
			{
				pos=pos+4;
				if (isdigit(conf[pos]))
				{
					mute = conf[pos]-48;
					pos++;
				}
			}
		}
		pos++;
	}
	return;
}


/* This function plays the file at program start only */
void playfirst(long int playpos)
{
	printf("\n1 <- 5 seeks backward");
	printf("\n6 -> 0 seeks forward");
	printf("\n ,  .  rewinds/skips one frame");
	printf("\n     a sets startpoint, A goes there");
	printf("\n     b sets endpoint, B goes there");
	printf("\n     s saves");
	printf("\n     p seeks to next silence");
	printf("\n\n     length of silence is %u milliseconds",silencelength);
	printf("\n     maximum volume during silence is %u ",silvol);
	printf("\n     using soundcard #%u ",card);
	printf("\n     sound is ");
	if (mute) printf("OFF \n");
	else printf("ON \n");
	printf("\n     Press 'h' for more help\n");
	printf("\n[1..0,r,a,b,s,q] >  ");
	playsel(playpos);
}


/* This function saves the selection non-interactively */
void savesel(char *prefix)
{
	char *outname=malloc(8191);
	int number=1;
	long bytesin=0;
	long endm, startm;
	double ends, starts;
	int a;
	FILE *fp;
	FILE *outfile;
// 	FILE *id3file;

	/* correct mins and secs */
	if(startsecs>59.999){ do {startmins++; startsecs=startsecs-60;} while (startsecs>59.999);}
	if(endsecs>59.999) { do {endmins++; endsecs=endsecs-60;} while (endsecs>59.999);}

	/* Import title from tags. Prefer V1 over V2!
	   No user interaction in this function */
	zaptitle();
	importid3(inpoint);
	importtag(outpoint);

	/* title known? */
	if (!no_tags && !forced_prefix && strlen(title)>0)
	{
		snprintf(outname,8190, "%s - %s.mp3",artist,title);
		fp = fopen(outname,"r");
		if (fp != NULL) /* file exists? */
		{
			while(fp != NULL) /* test if file exists */
			{
				number++;
				fclose(fp);
				if (number>99)
				{
					printf("\n\nFilename already exists 99 times.\nPlease save with tag (press t) and choose another title. \n");
					return;
				}
				snprintf(outname,8190, "%s - %s_%02u.mp3",artist,title,number);
				fp = fopen(outname,"r");
			}
			number=number-overwrite; /* step one number back in case of overwrite mode */
			if (number==1) snprintf(outname,8190, "%s - %s.mp3",artist,title); /* overwrite file without number */
			else snprintf(outname,8190, "%s - %s_%02u.mp3",artist,title,number);
		}
	}
	else
	/* title not known */
	{
		/* outname = prefix number . suffix */
		snprintf(outname,8190, "%s%04u.mp3",prefix,number);

	if (inpoint > filesize)
	{
		if (nonint==1)
		{
			fprintf(stdout,"  cutmp3: startpoint (%u:%05.2f) must be before end of file (%u:%05.2f)!  \n",showmins(inpoint),showsecs(inpoint),showmins(filesize),showsecs(filesize));
			return;
		}
		else
		{
			printf("  ERROR: startpoint (%u:%05.2f) must be before end of file (%u:%05.2f)!  \n",showmins(inpoint),showsecs(inpoint),showmins(filesize),showsecs(filesize));
			return;
		}
	}
	if (inpoint < audiobegin)
	{
		if (nonint==1)
		{
			fprintf(stdout,"  cutmp3: setting startpoint (%u:%05.2f) to beginning of file (%u:%05.2f)!  \n",showmins(inpoint),showsecs(inpoint),showmins(audiobegin),showsecs(audiobegin));
			inpoint=audiobegin;
			return;
		}
		else
		{
			printf("  WARNING: setting startpoint (%u:%05.2f) to beginning of file (%u:%05.2f)!  \n",showmins(inpoint),showsecs(inpoint),showmins(audiobegin),showsecs(audiobegin));
			inpoint=audiobegin;
		}
	}
	if (outpoint > filesize)
	{
		if (nonint==1)
		{
// 			printf("%lu %lu",outpoint,filesize);
			fprintf(stdout,"  cutmp3: setting endpoint (%u:%05.2f) to end of file (%u:%05.2f)!  \n",showmins(outpoint),showsecs(outpoint),showmins(filesize),showsecs(filesize));
// 			return;
		}
		else
		{
			printf("  WARNING: setting endpoint (%u:%05.2f) to end of file (%u:%05.2f)!  \n",showmins(outpoint),showsecs(outpoint),showmins(filesize),showsecs(filesize));
		}
		outpoint=filesize;
	}
	if (outpoint <= inpoint)
	{
		if (nonint==1)
		{
			fprintf(stdout,"  cutmp3: endpoint (%u:%05.2f) must be after startpoint (%u:%05.2f)!  \n",showmins(outpoint),showsecs(outpoint),showmins(inpoint),showsecs(inpoint));
			return;
		}
		else
		{
			printf("  ERROR: endpoint (%u:%05.2f) must be after startpoint (%u:%05.2f)!  \n",showmins(outpoint),showsecs(outpoint),showmins(inpoint),showsecs(inpoint));
			return;
		}
	}

		/* if file exists, increment number */
		fp = fopen(outname,"r");
		while(fp != NULL) /* test if file exists */
		{
			number++;
			fclose(fp);
			if (number>9999) usage("9999 files written. Please choose another prefix via '-o prefix.'");
			snprintf(outname,8190, "%s%04u.mp3",prefix,number);
			fp = fopen(outname,"r");
		}
		number=number-overwrite; /* step one number back in case of overwrite mode */
		if (number==0) number=1;
		snprintf(outname,8190, "%s%04u.mp3",prefix,number);
	}

	/* forced output file name used? */
	if (forced_file==1) snprintf (outname, 8190, forcedname);

	/* open outfile */
// 	if (NULL== (outfile = fopen(outname,"wb"))){perror("\ncutmp3: cannot not write output file! read-only filesystem?");exitseq(10);}
	if (stdoutwrite!=1 && NULL== (outfile = fopen(outname,"wb"))){perror("\ncutmp3: cannot not write output file! read-only filesystem?");exitseq(10);}

	/* copy id3 in case of -c switch */
	if (copytags==1 && importid3(0)>0)
	{
// 		id3file = fopen("/tmp/id3v2tag","rb");
		fseek(id3file2, 0, SEEK_SET);
		while( (a=fgetc(id3file2)) != EOF ) fputc(a,outfile);
		if (stdoutwrite==1) putchar(a);
		else fputc(a,outfile);
// 		fclose(id3file);
	}

	/* COPY DATA HERE */
	fseek(mp3file, inpoint, SEEK_SET);
	for (bytesin=0 ; bytesin < outpoint-inpoint ; bytesin++)
	{
		if(stdoutwrite==1) putchar(getc(mp3file));
// 		if(stdoutwrite==1) fputs(getc(mp3file),stdout);
		else fputc(getc(mp3file),outfile);
	}

	/* copy tag in case of -c switch */
	if (copytags==1 && importtag(filesize)>0)
	{
// 		id3file = fopen("/tmp/id3v1tag","rb");
		fseek(id3file1, 0, SEEK_SET);
		while( (a=fgetc(id3file1)) != EOF )
		{
			if(stdoutwrite==1) putchar(a);
			else fputc(a,outfile);
		}
// 		fclose(id3file);
	}

	/* close outfile */
	if (stdoutwrite!=1) fclose(outfile);

 	if (stdoutwrite==1) /* write to STDOUT, return without printing messages */
 	{
		overwrite=0;
		zaptitle(); /* erase title name after saving */
		free(outname);
		return;
	}

	/* noninteractive cutting: */
	if (nonint==1 && negstart>0 && negend>0) printf("  saved %lu:%05.2f - %lu:%05.2f to '%s'.  \n",startmins,startsecs,endmins,endsecs,outname);
	else
	/* negative offset: */
	if (nonint==1)
	{
		if (negend<0)
		{
			endm=totalmins-endmins;
			ends=totalsecs-endsecs;
			if (ends<0){ends=ends+60; endm--;}
		}
		else {endm=endmins; ends=endsecs;}
		if (negstart<0)
		{
			startm=totalmins-startmins;
			starts=totalsecs-startsecs;
			if (starts<0){starts=starts+60; startm--;}
		}
		else {startm=startmins; starts=startsecs;}
		printf("  saved approx. %i:%05.2f - %i:%05.2f to '%s'.  \n",showmins(inpoint),showsecs(inpoint),showmins(outpoint),showsecs(outpoint),outname);
	}
	else
	/* interactive cutting: */
	printf("  saved %u:%05.2f - %u:%05.2f to '%s'.  \n",showmins(inpoint),showsecs(inpoint),showmins(outpoint),showsecs(outpoint),outname);

	overwrite=0;
	zaptitle(); /* erase title name after saving */
	free(outname);
	return;
}


/* This function saves the selection interactively with ID3 tags */
void savewithtag(void)
{
	char *tempartist=malloc(8191);
	char *temptitle=malloc(8191);
	char *tempalbum=malloc(8191);
	char *tempyear=malloc(8191);
	char *tempcomment=malloc(8191);
	char *title1=malloc(8191); /* Title from Tag V1 */
	char *title2=malloc(8191); /* Title from Tag V2 */
	int i, a, tagver=0, hasid3=0, hastag=0, number=1;
	char outname[8191]="cutmp3.tmp";
//	char *oldname=outname;
	char outname2[8191]="\0";
	char *newname=outname2;
	char *tmp;
	FILE *fp;
// 	FILE *id3file;
	FILE *outfile;
	long oldinpoint=inpoint;
	long oldoutpoint=outpoint;
	long bytesin;

	temptitle[0]='\0';
	tempartist[0]='\0';
	tempalbum[0]='\0';
	tempyear[0]='\0';
	tempcomment[0]='\0';
	title1[0]='\0';
	title2[0]='\0';

	if (inpoint > filesize)
	{
		printf("  ERROR: startpoint (%u:%05.2f) must be before end of file (%u:%05.2f)!  \n",showmins(inpoint),showsecs(inpoint),showmins(filesize),showsecs(filesize));
		return;
	}
	if (inpoint < audiobegin)
	{
		printf("  WARNING: setting startpoint (%u:%05.2f) to beginning of file (%u:%05.2f)!  \n",showmins(inpoint),showsecs(inpoint),showmins(audiobegin),showsecs(audiobegin));
		inpoint=audiobegin;
	}
	if (outpoint > filesize)
	{
		printf("  WARNING: setting endpoint (%u:%05.2f) to end of file (%u:%05.2f)!  \n",showmins(outpoint),showsecs(outpoint),showmins(filesize),showsecs(filesize));
		outpoint=filesize;
	}
	if (outpoint <= inpoint)
	{
		printf("  ERROR: endpoint (%u:%05.2f) must be after startpoint (%u:%05.2f)!  \n",showmins(outpoint),showsecs(outpoint),showmins(inpoint),showsecs(inpoint));
		return;
	}

	/*********************************/
	/* Choose ID3 V2 or V1 or custom */
	/*********************************/
	hasid3=hastag=0;
	zaptitle(); importtag(outpoint);
	if (strlen(title)>0) {hastag=1; snprintf(title1,31,title);} /* check if selection has tag v1 */
	zaptitle(); importid3(inpoint);
	if (strlen(title)>0) {hasid3=1; snprintf(title2,31,title);} /* check if selection has tag v2 */
	if (hastag+hasid3==2) /* has both tags in selection */
	{
		printf("\n\nImported titles from selection:");
		printf("\nPress 1 for this title: %s",title1);
		printf("\nPress 2 for this title: %s",title2);
		printf("\nOr press any other key for a custom tag  ");
		tagver=getchar();
		if (tagver=='1')
		{
			importtag(outpoint);
		}
		else if (tagver=='2')
		{
			importid3(inpoint);
		}
	}
	else if (hasid3>hastag)   /* only V2 */
	{
		importid3(inpoint);
		tagver='2';
	}
	else if (hastag>hasid3)  /* only V1 */
	{
		importtag(outpoint);
		tagver='1';
	}

	else if (hastag+hasid3==0) /* no tag at all, get them from whole file */
	{
		hasid3=hastag=0;
		zaptitle(); importtag(filesize); if (strlen(title)>0) {hastag=1; snprintf(title1,31,title);}
		zaptitle(); importid3(0);        if (strlen(title)>0) {hasid3=1; snprintf(title2,31,title);}
		if (hastag+hasid3==2) /* whole file has both tags */
		{
 			printf("\n\nImported titles from %s:",filename);
			printf("\nPress 1 for this title: %s",title1);
			printf("\nPress 2 for this title: %s",title2);
			printf("\nOr press any other key for a custom tag  ");
			tagver=getchar();
			if (tagver=='1') importtag(filesize);
			else if (tagver=='2') importid3(0);
		}
		else if (hasid3>hastag)         /* has V2 info from whole file */
		{
			importid3(0);
 			printf("\n\nImported title from %s:",filename);
			printf("\nPress 2 for this title: %s",title);
			printf("\nOr press any other key for a custom tag  ");
			tagver=getchar();
			if (tagver!='2') tagver='3'; /* info has already been imported in this block */
		}
		else if (hastag>hasid3)        /* has V1 info from whole file  */
		{
			importtag(filesize);
 			printf("\n\nImported title from %s:",filename);
			printf("\nPress 1 for this title: %s",title);
			printf("\nOr press any other key for a custom tag  ");
			tagver=getchar();
			if (tagver!='1') tagver='3'; /* info has already been imported in this block */
		}
	}

	/* forced output file name used? */
	if (forced_file==1)
	{
		snprintf (outname, 8190, forcedname);
	}

	/*******************/
	/* file write part */
	/*******************/
	if (NULL== (outfile = fopen(outname,"wb"))){perror("\ncutmp3: cannot not write output file! read-only filesystem?");exitseq(10);}

	printf("\nwriting audio data...");

	if (tagver=='1') /* if chosen to copy ID3 V1 */
	{
		inpoint=inpoint+importid3(inpoint); /* remove possible id3 at beginning */
		outpoint=outpoint-importtag(outpoint); /* remove possible tag at end */
		fseek(mp3file, inpoint, SEEK_SET);
		/* copy audio data */
		for (bytesin=0; bytesin < outpoint-inpoint; bytesin++) fputc(getc(mp3file),outfile);
		/* copy id3 v1 tag */
// 		id3file = fopen("/tmp/id3v1tag","rb");
		fseek(id3file1, 0, SEEK_SET);
		while(( a=fgetc(id3file1)) != EOF ) fputc(a,outfile);
// 		fclose(id3file);
		printf(" finished.\n");
	}
	else if (tagver=='2') /* if chosen to copy ID3 V2 */
	{
		/* copy id3 v2 tag */
// 		id3file = fopen("/tmp/id3v2tag","rb");
		fseek(id3file2, 0, SEEK_SET);
		while(( a=fgetc(id3file2)) != EOF ) fputc(a,outfile);
// 		fclose(id3file);
		outpoint=outpoint-importtag(outpoint); /* remove possible tag at end */
		inpoint=inpoint+importid3(inpoint); /* remove possible id3 at beginning */
		/* copy audio data */
		for (bytesin=0; bytesin < outpoint-inpoint; bytesin++) fputc(getc(mp3file),outfile);
		printf(" finished.\n");
	}
	else /* custom tag */
	{
		inpoint=inpoint+importid3(inpoint); /* remove possible id3 at beginning */
		outpoint=outpoint-importtag(outpoint); /* remove possible tag at end */

		fseek(mp3file, inpoint, SEEK_SET);

		/* copy audio data */
		for (bytesin=0; bytesin < outpoint-inpoint; bytesin++) fputc(getc(mp3file),outfile);

		printf(" finished.");

		/* write tag here */
		fputs("TAG",outfile);

		printf("\n\nPress <ENTER> for '%s'",temptitle);
		tmp=readline("\nTitle? ");
		tmp[30]='\0';
		if (strlen(tmp)>0) {snprintf(temptitle,31,tmp);}
		else snprintf(tmp,31,temptitle); /* recycle old title name when hitting ENTER */
		tmp[30]='\0';
		fprintf(outfile,tmp);
		for (i=strlen(tmp) ; i<30 ; i++) fputc(32,outfile);
		free(tmp);

		printf("\nPress <ENTER> for '%s'",tempartist);
		tmp=readline("\nArtist? ");
		tmp[30]='\0';
		if (strlen(tmp)>0) {snprintf(tempartist,31,tmp);}
		else snprintf(tmp,31,tempartist); /* recycle old artist name when hitting ENTER */
		tmp[30]='\0';
		fprintf(outfile,tmp);
		for (i=strlen(tmp) ; i<30 ; i++) fputc(32,outfile);
		free(tmp);

		printf("\nPress <ENTER> for '%s'",tempalbum);
		tmp=readline("\nAlbum? ");
		tmp[30]='\0';
		if (strlen(tmp)>0) {snprintf(tempalbum,31,tmp);}
		else snprintf(tmp,31,tempalbum); /* recycle old album name when hitting ENTER */
		tmp[30]='\0';
		fprintf(outfile,tmp);
		for (i=strlen(tmp) ; i<30 ; i++) fputc(32,outfile);
		free(tmp);

		printf("\nPress <ENTER> for '%s'",tempyear);
		tmp=readline("\nYear? ");
		tmp[4]='\0';
		if (strlen(tmp)>0) {snprintf(tempyear,5,tmp);}
		else snprintf(tmp,5,tempyear); /* recycle old year when hitting ENTER */
		tmp[4]='\0';
		fprintf(outfile,tmp);
		for (i=strlen(tmp) ; i<4 ; i++) fputc(32,outfile);
		free(tmp);

		printf("\nPress <ENTER> for '%s'",tempcomment);
		tmp=readline("\nComment? ");
		tmp[30]='\0';
		if (strlen(tmp)>0) {snprintf(tempcomment,31,tmp);}
		else snprintf(tmp,31,tempcomment); /* recycle old comment when hitting ENTER */
		tmp[30]='\0';
		fprintf(outfile,tmp);
		for (i=strlen(tmp) ; i<30 ; i++) fputc(32,outfile);
		free(tmp);

		fputc(genre,outfile);
		genre=-1;

		snprintf(title,31,temptitle);
		snprintf(artist,31,tempartist);
	}

	fclose(outfile);

	/* restore them: */
	inpoint=oldinpoint;
	outpoint=oldoutpoint;

	/********************/
	/* file rename part */
	/********************/
	snprintf(newname,8190, "%s - %s.mp3",artist,title);

	fp = fopen(newname,"r");
	if (fp != NULL) /* file exists? */
	{
		while(fp != NULL) /* test if file exists */
		{
			number++;
			fclose(fp);
			if (number>99)
			{
				printf("\n  File NOT written. Please choose another title.  \n");
				return;
			}
			snprintf(newname,8190, "%s - %s_%02u.mp3",artist,title,number);
			fp = fopen(newname,"r");
		}
		number=number-overwrite; /* step one number back in case of overwrite mode */
		if (number==1) snprintf(newname,8190, "%s - %s.mp3",artist,title); /* overwrite file without number */
		else snprintf(newname,8190, "%s - %s_%02u.mp3",artist,title,number);
	}
	/* forced output file name used? Then show correct file name in summary. */
	if (forced_file==1) snprintf (newname, 8190, forcedname);
	/* rename file only if not forced name: **
	** Only after writing ID3 tag we know the name, so it must be renamed after writing. */
	else rename(outname, newname);

	if (tagver=='2') printf("\n  saved %u:%02.0f - %u:%02.0f with ID3 V2 tag to '%s'.  \n",showmins(inpoint),showsecs(inpoint),showmins(outpoint),showsecs(outpoint),newname);
	else printf("\n  saved %u:%02.0f - %u:%02.0f with ID3 V1 tag to '%s'.  \n",showmins(inpoint),showsecs(inpoint),showmins(outpoint),showsecs(outpoint),newname);

	overwrite=0;
	free(tempartist);
	free(temptitle);
	free(tempalbum);
	free(tempyear);
	free(tempcomment);
	free(title1);
	free(title2);
	return;
}


/* ################### KEY PRESS ######################## */

static struct termios  current,  /* new terminal settings             */
                       initial;  /* initial state for restoring later */

/* Restore term-settings to those saved when term_init was called */
void  term_restore  (void)  {
  tcsetattr(0, TCSANOW, &initial);
}  /* term_restore */

/* Clean up terminal; called on exit */
void  term_exit  ()  {
	term_restore();
	printf("\n\nbugreports to mail@puchalla-online.de\n\n");
	exitseq(0);
}	/* term_exit */

/* Will be called when ctrl-z is pressed, this correctly handles the terminal */
void  term_ctrlz  ()  {
	signal(SIGTSTP, term_ctrlz);
	term_restore();
	kill(getpid(), SIGSTOP);
}  /* term_ctrlz */

/* Will be called when application is continued after having been stopped */
void  term_cont  ()  {
	signal(SIGCONT, term_cont);
	tcsetattr(0, TCSANOW, &current);
}  /* term_cont */

/* Needs to be called to initialize the terminal */
void  term_init  (void)  {
	/* if stdin isn't a terminal this fails. But    */
	/* then so does tcsetattr so it doesn't matter. */

	tcgetattr(0, &initial);
	current = initial;                      /* Save a copy to work with later  */
	signal(SIGINT,  term_exit);             /* We _must_ clean up when we exit */
	signal(SIGQUIT, term_exit);
	signal(SIGTSTP, term_ctrlz);            /* Ctrl-Z must also be handled     */
	signal(SIGCONT, term_cont);
	//atexit(term_exit);
}	/* term_init */

/* Set character-by-character input mode */

void  term_character  (void)  {
	/* One or more characters are sufficient to cause a read to return */
	current.c_cc[VMIN]   = 1;
	current.c_cc[VTIME]  = 0;  /* No timeout; read forever until ready */
	current.c_lflag     &= ~ICANON;           /* Line-by-line mode off */
	tcsetattr(0, TCSANOW, &current);
}  /* term_character */

/* Return to line-by-line input mode */
void  term_line  (void)  {
	current.c_cc[VEOF]  = 4;
	current.c_lflag    |= ICANON;
	tcsetattr(0, TCSANOW, &current);
}  /* term_line */

/* Key pressed ? */
int  kbhit  (void)  {
	struct timeval  tv;
	fd_set          read_fd;

	/* Do not wait at all, not even a microsecond */
	tv.tv_sec  = 0;
	tv.tv_usec = 0;

	/* Must be done first to initialize read_fd */
	FD_ZERO(&read_fd);

	/* Makes select() ask if input is ready;  0 is file descriptor for stdin */
	FD_SET(0, &read_fd);

	/* The first parameter is the number of the largest fd to check + 1 */
	if (select(1, &read_fd,
		NULL,         /* No writes        */
		NULL,         /* No exceptions    */
		&tv)  == -1)
	return(0);                 /* an error occured */

	/* read_fd now holds a bit map of files that are readable. */
	/* We test the entry for the standard input (file 0).      */
	return(FD_ISSET(0, &read_fd)  ?  1  :  0);
}

/* ------------------------------------------------------------------------- */


/* This function seeks a time from EOF */
long seekfromend(double minn, double secc)
{
	long pos=filesize;
	long double time=0;

	fseek(mp3file, filesize, SEEK_SET);
	while(1)                              /* loop till break */
	{
		if (time>=minn*60+secc) break;       /* inpoint reached */
		if (pos<=audiobegin)                  /* start of file reached */
		{
			printf("  WARNING: beginning of file reached!  \n");
			pos=audiobegin;
			break;
		}
		pos=prevframe(pos);
		time=time+fix_frametime/1000;        /* sum up real time, time of one frame in seconds is 1152/sf */
	}
	return pos;
}

/* This function copies by counting frames */
void cutexact(char *prefix)
{
	long pos=audiobegin;
	unsigned char a,b,c,d;
	long double time=0;


	/* first look for inpoint */

//	printf("  seeking startpoint");
	if (negstart<0)
	{
		printf("  seeking startpoint backwards... (slow!)\n");
		inpoint=seekfromend(startmins,startsecs);
	}
	else
	{
		fseek(mp3file, audiobegin, SEEK_SET);
		while(1)                              /* loop till break */
		{
			if (time>=startmins*60+startsecs) break;       /* inpoint reached */
			if (pos>=filesize) return;                     /* EOF reached */
			a=fgetc(mp3file); pos++;
			if (a==255)
			{
				b=fgetc(mp3file);
				c=fgetc(mp3file);
				d=fgetc(mp3file);
//				if (isheader(b,c,d)==1 && framesize(b,c,d)!=1)              /* next frame found */
				if (is_header(b,c,d))     /* next frame found */
				{
//					printf("found header\n");
					fseek(mp3file, pos-1+framesize(b,c,d), SEEK_SET);    /* skip framesize bytes */
					pos=pos-1+framesize(b,c,d);                          /* adjust pos to actual SEEK_SET */
					time=time+fix_frametime/1000;                        /* sum up real time, time of one frame in seconds is 1152/sf */
				}
				else fseek(mp3file, pos, SEEK_SET);    /* rewind to next byte for if (a==255) */
			}
		}
		inpoint=pos;
	}
// 	printf("  inpoint=%lu",inpoint);

	/* inpoint reached, now search for outpoint */

//	printf("  seeking endpoint");
	pos=audiobegin;
	time=0;
	if (negend<0)
	{
		printf("  seeking endpoint backwards... (slow!)\n");
		outpoint=seekfromend(endmins,endsecs);
	}
	else
	{
		while(1)                              /* loop till break */
		{
			/* pos must point to end of header in case next if() is true */
			if (time>=endmins*60+endsecs) break;       /* outpoint reached */
			if (pos>=filesize) break;                  /* EOF reached */
			a=fgetc(mp3file); pos++;
			if (a==255)
			{
				b=fgetc(mp3file);
				c=fgetc(mp3file);
				d=fgetc(mp3file);
				if (is_header(b,c,d))     /* next frame found */
				{
					fseek(mp3file, pos-1+framesize(b,c,d), SEEK_SET);    /* skip framesize bytes */
					pos=pos-1+framesize(b,c,d);                          /* adjust pos to actual SEEK_SET */
					time=time+fix_frametime/1000;                        /* sum up real time */
				}
				else fseek(mp3file, pos, SEEK_SET);    /* rewind to next byte for if (a==255) */
			}
		}
		outpoint=pos;
		if (outpoint>filesize) outpoint=filesize;
	}

	/* outpoint reached, now write file */

//	printf("  saving selection...\n");
	savesel(prefix);
//	printf("%u %u %u %u",startsecs,endsecs,inpoint,outpoint);
	startsecs=startmins=endsecs=endmins=0;
	return;
}


/* This function writes a timetable when -a and -b is used */
void writetable()
{
	int fd=-1;

	if ((fd = mkstemp(tblname)) == -1 || (NULL== (tblfile = fdopen(fd, "w+b"))) )
	{
		perror("\ncutmp3: failed writing temporary timetable in /tmp");exitseq(11);
	}
	fprintf(tblfile, userin);
	fprintf(tblfile, " ");
	fprintf(tblfile, userout);
	fprintf(tblfile, "\n");
	fclose(tblfile);
	a_b_used=1;
	return;
}


/* This function copies selections when using a timetable */
void cutfromtable(char *tablename, char *prefix)
{
	int pos2, position=1, linenumber=1;
	double number=0;
	startsecs=startmins=endsecs=endmins=0;
	negstart=negend=1;

	timefile = fopen(tablename,"rb");
	/* Load BUFFER Bytes into unsigned buffer[] */
	for (pos2=0 ; pos2<BUFFER ; pos2++) ttable[pos2]=fgetc(timefile);
	fclose(timefile);

	pos2=0;
	do
	{
		/* position: startmins=1 startsek=2 startten=3 starthun=4 */
		/*             endmins=5   endsek=6   endten=7   endhun=8 */
		number=ttable[pos2]-48;
		if (isdigit(ttable[pos2])) /* char is a digit */
		{
			switch (position)
			{	case 1: startmins=startmins*10+ttable[pos2]-48; break;
				case 2: startsecs=startsecs*10+ttable[pos2]-48; break;
				case 3: startsecs=startsecs+number/10; position++; break;
				case 4: startsecs=startsecs+number/100; do pos2++; while (!isspace(ttable[pos2])); break;
				case 5: endmins=endmins*10+ttable[pos2]-48; break;
				case 6: endsecs=endsecs*10+ttable[pos2]-48; break;
				case 7: endsecs=endsecs+number/10; position++; break;
				case 8: endsecs=endsecs+number/100; position++; break;
				default: break;
			}
		}

		/* '-' is offset from end */
		if (ttable[pos2]==45 && position < 5) negstart=-1;
		if (ttable[pos2]==45 && position > 4) negend=-1;
		/* ':' and '.' increase position */
		if (ttable[pos2]==58 && position < 5) {position=2;} /* : */
		if (ttable[pos2]==58 && position > 4) {position=6;} /* : */
		if (ttable[pos2]==46 && position==1) {startsecs=startmins; startmins=0; position=3;} /* . */
		if (ttable[pos2]==46 && position==2) {position=3;} /* . */
		if (ttable[pos2]==46 && position==5) {endsecs=endmins; endmins=0; position=7;} /* . */
		if (ttable[pos2]==46 && position==6) {position=7;} /* . */

		/* space switches to read end or to cutexact() */
		if (isspace(ttable[pos2]))  /* char is a whitespace */
		{
			if (position==5 && endmins+endsecs > 0)  /* only minutes given, finish */
			{
				position=9;
			}
			if (position < 5) position=5;  /* read end now */
			if (position > 4 && endmins+endsecs > 0) /* cut exact now */
			{
				/* correct mins and secs */
				if(startsecs>59.999){ do {startmins++; startsecs=startsecs-60;} while (startsecs>59.999);}
				if(endsecs>59.999) { do {endmins++; endsecs=endsecs-60;} while (endsecs>59.999);}

				if (a_b_used!=1) printf(" using timetable \"%s\" line %i:\n",tablename,linenumber);linenumber++;
				if (endmins*negend>totalmins)
				{
					printf("  WARNING: setting endpoint (%li:%05.2f) to end of file (%u:%05.2f)!  \n",endmins,endsecs,showmins(filesize),showsecs(filesize));
					endmins=totalmins;
					endsecs=totalsecs;
				}
				if (endmins*negend==totalmins && endsecs>totalsecs)
				{
// 					printf("%lu %lu",outpoint,filesize);
					printf("  WARNING: setting endpoint (%li:%05.2f)  to end of file (%u:%05.2f)!  \n",endmins,endsecs,showmins(filesize),showsecs(filesize));
					endsecs=totalsecs;
				}
				if (startmins*negstart>totalmins)
				{
					printf("  ERROR: startpoint (%li:%05.2f) is after end of file (%u:%05.2f)!  \n",startmins,startsecs,showmins(filesize),showsecs(filesize));
				}
				if (startmins*negstart==totalmins && startsecs>totalsecs)
				{
					printf("  ERROR: startpoint (%li:%05.2f)  is after end of file (%u:%05.2f)!  \n",startmins,startsecs,showmins(filesize),showsecs(filesize));
				}
				/* end after start? -> check in cutexact() */
/*				if (startmins*60+startsecs>=endmins*60+endsecs && negstart==1 && negend==1)
				{
					fprintf(stdout,"  cutmp3: endpoint (%li:%05.2f) must be after startpoint (%li:%05.2f)!  \n",endmins,endsecs,startmins,startsecs);
				}*/
				else cutexact(prefix);
				position=1;
				startsecs=startmins=endsecs=endmins=0;
				negstart=negend=1;
				number=0;
			}
		}

		if (ttable[pos2]==255) pos2=BUFFER;
//		printf("pos2=%u %lu:%02.2f %lu:%02.2f position=%u\n",pos2,startmins,startsecs,endmins,endsecs,position);
		pos2++;
	}
	while (pos2<BUFFER);
}


void showfileinfo(int rawmode, int fix_channels)
{
	long double totalframes;
	unsigned char a,b,c;

	if (rawmode==1)
	{
		totalframes=get_total_frames();
		printf("%.0Lf %u %u %.0f %u %.0Lf\n",totalframes,fix_sampfreq,fix_channels,avbr,fix_mpeg,fix_frametime*totalframes);
		exitseq(0);
	}

	printf("\n\n* Properties of \"%s\":\n* \n",filename);
	if (audiobegin != 0) printf("* Audio data starts at %u Bytes\n",audiobegin);
	printf("* Size of first frame is %u Bytes\n",framesize(begin[1],begin[2],begin[3]));
	if (mpeg(begin[1])==3) printf("* File is MPEG2.5-Layer%u\n",layer(begin[1]));
	else printf("*\n* Format is MPEG%u-Layer%u\n",mpeg(begin[1]),layer(begin[1]));
	if (vbr==0) printf("*           %.0f kBit/sec\n",avbr);
	else printf("*           %.2f kBit/sec (VBR average)\n",avbr);
	printf("*           %u Hz ",sampfreq(begin[1],begin[2]));
	if (channelmode(begin[3])==0) printf("Stereo\n");
	if (channelmode(begin[3])==1) printf("Joint-Stereo\n");
	if (channelmode(begin[3])==2) printf("Dual-Channel\n");
	if (channelmode(begin[3])==3) printf("Mono\n");
	if (vbr==0) printf("*\n* Filelength is %u minute(s) %4.2f second(s)\n",showmins(filesize-audiobegin),showsecs(filesize-audiobegin));
	else printf("*\n* Filelength is approximately %u minute(s) %2.0f second(s)\n",showmins(filesize-audiobegin),showsecs(filesize-audiobegin));

	/* show tag V1 if any: */
	fseek(mp3file, filesize-128, SEEK_SET); /* set to right position */
	a=fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);
	if (a=='T' && b=='A' && c=='G' ) infotag(filesize); /* TAG is there */
	else printf("\nno ID3 V1 tag.\n");

	/* show tag V2 if any: */
	zaptitle(); importid3(0);
	if (strlen(title)>0)
	{
		printf("\nID3 V2 tag:\nTitle:   %s",title);
		printf("\nArtist:  %s",artist);
		printf("\nAlbum:   %s",album);
		printf("\nYear:    %s",year);
		printf("\nComment: %s\n",comment);
	}
	else printf("\nno ID3 V2 tag.\n");
	return;

}


void showdebug(long seekpos, char *prefix)
{
	printf("\n\nI am at offset %li in %s\n",seekpos,filename);
	printf("Prefix is '%s'\n",prefix);
}


/* ------------------------------------------------------------------------- */
/*      */
/* MAIN */
/*      */
/* ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	int a,b,c,d,char1,showinfo=0,rawmode=0,fix_channels=0;
	long int pos, startpos;
	double ft_factor=2;
	char *tablename=malloc(8191);
	char *prefix=malloc(8191);
	void (*play)(long int playpos) = playsel;

/* ------------------------------------------------------------------------- */
/*                   */
/* parse commandline */
/*                   */
/* ------------------------------------------------------------------------- */

	if ((fd1 = mkstemp(id3name1)) == -1 || (NULL== (id3file1 = fdopen(fd1, "w+b"))) )
	{
		perror("\ncutmp3: failed writing temporary id3v1 file in /tmp");exitseq(8); /* open temp.id3.file read-writable zero length */
	}
	if ((fd2 = mkstemp(id3name2)) == -1 || (NULL== (id3file2 = fdopen(fd2, "w+b"))) )
	{
		perror("\ncutmp3: failed writing temporary id3v2 file in /tmp");exitseq(7); /* open temp.id3.file read-writable zero length */
	}
	if ((fdlog = mkstemp(logname)) == -1 || (NULL== (logfile = fdopen(fdlog, "w+b"))) )
	{
		perror("\ncutmp3: failed writing temporary logfile in /tmp");exitseq(11);
	}

	readconf();       /* read configuration file first! */

	if (argc<2) {usage("ERROR: missing filename");exitseq(1);}
	else
	{
		while ((char1 = getopt(argc, argv, "cCheqFf:a:b:i:o:O:I:d:D:s:")) != -1)
		{
			switch (char1)
			{
				case 'a':
					if (optarg!=0) userin=optarg;
//					else usage("Error: missing time argument");
					break;
				case 'b':
					if (optarg!=0) userout=optarg;
//					else usage("Error: missing time argument");
					break;
				case 'c':
					copytags=1;
					break;
				case 'C':
					no_tags=1;
					break;
				case 'd':
					if (optarg!=0) card=atoi(optarg);
					break;
				case 'D':
					if (optarg!=0) debug=atoi(optarg);
					break;
				case 'e':
					exactmode=1;
					break;
				case 'f':
					if (NULL == (timefile = fopen(optarg,"rb") ) )
					{
						perror(optarg);
						exitseq(3);
					}
					else tablename=optarg; hastable=1;
					break;
				case 'h':
					usage("");
					exit(0);
				case 'q':
					mute=1;
					break;
				case 'i':
//					if (optarg==0) usage("Error: option requires an argument");
					if (NULL == (mp3file = fopen(optarg,"rb") ) )
					{
						perror(optarg);
						exitseq(2);
					}
					else filename=optarg;
					break;
				case 'I':
//					if (optarg==0) usage("Error: option requires an argument");
					if (NULL == (mp3file = fopen(optarg,"rb") ) )
					{
						perror(optarg);
						exitseq(2);
					}
					else { filename=optarg; showinfo=1; }
					break;
				case 'F':
					rawmode=1;
					break;
				case 'o':
					if (optarg!=0) snprintf(prefix,8190,optarg);
//					else usage("Error: missing outputprefix");
					forced_prefix=1;
					break;
/* ThOr: let user define exactly one output file */
				case 'O':
					if (optarg!=0)
					{
						forcedname=optarg;
						forced_file=1;
						if(optarg[0]=='-') stdoutwrite=1;
					}
					break;
				case 's':
					if (optarg!=0) silfactor=atoi(optarg); /* changes maximum silence length */
					break;
				default:
					exitseq(1);
					break;
			}
		}
	}

	if (strlen(prefix)==0) prefix="result";

	if (userin==0 && userout!=0) usage("ERROR: missing inpoint");
	if (userin!=0 && userout==0) usage("ERROR: missing outpoint");
	if (filename==0) {usage("ERROR: missing filename, use 'cutmp3 -i file.mp3 [options]'");exitseq(1);}

	if (stdoutwrite==0) setvbuf(stdout,NULL,_IONBF,0); /* switch off buffered output to stdout when writing to file */

	mp3file = fopen(filename, "rb");

	/* get filesize and set outpoint to it */
	fseek(mp3file, 0, SEEK_END);
	outpoint=filesize=ftell(mp3file);
//  printf("Filesize : %ld\n", filesize);

	/* Check beginning of file: set audiobegin */
	if ( (audiobegin=skipid3v2(0)) != 0 ) ;
	else
	{
		fseek(mp3file, 0, SEEK_SET);
		a=fgetc(mp3file);
		b=fgetc(mp3file);
		c=fgetc(mp3file);
		d=fgetc(mp3file);
		if (framesize(b,c,d)==nextframe(0)) audiobegin=0;
		else audiobegin=nextframe(0);
	}

	audiobegin=nextframe(audiobegin-1); /* in case there is invalid data after an ID3 V2 tag */

	startpos=inpoint=audiobegin;

	fseek(mp3file, audiobegin, SEEK_SET);
	/* Load BUFFER Bytes into unsigned begin[]
	   begin[] starts at audiobegin, so contents should be fine */
	for (pos=0 ; pos<BUFFER && audiobegin+pos<=filesize ; pos++) begin[pos]=fgetc(mp3file);

//	printf("\naudiobegin=%u \n",audiobegin);

/********** global values: **********/

	fix_secondbyte=begin[1];
	fix_sampfreq=sampfreq(begin[1],begin[2]);
	if (fix_sampfreq==1) usage("ERROR: Could not determine sampling frequency.");
	fix_channelmode=channelmode(begin[3]);
	fix_channels=channels(begin[3]);
	fix_mpeg=mpeg(begin[1]);
	if (fix_mpeg==1) ft_factor=1;
	else ft_factor=2;

	fix_frametime=1152000.0/(double)fix_sampfreq/ft_factor;  /* in msecs, half the time if MPEG>1 */

	avbr=avbitrate(); /* average bitrate */
	if (avbr==0) {usage("ERROR: File has less than two valid frames.");exitseq(4);}
	msec=avbr/8;  /* one millisecond */
	dataend=prevframe(filesize); /* This may not be accurate! */

//	printf("dataend=%lu  filesize=%lu  nextframe(dataend)=%lu",dataend,filesize,nextframe(dataend));

/********** End of global part, now do specific parts **********/

	/* Show file info only? */
	if (showinfo==1)
	{
		showfileinfo(rawmode,fix_channels);
		printf("\n");
		exitseq(0);
	}

	/* get total mins:secs */
	totalmins=showmins(filesize-audiobegin);
	totalsecs=showsecs(filesize-audiobegin);

	/* -a and -b used? */
	if (userin!=0 && userout!=0)
	{
		nonint=1;
		writetable();
		cutfromtable(tblname,prefix);
		remove(tblname);
		exitseq(0);
	}

	/* timetable used? */
	if (hastable==1)
	{
		nonint=1;
		cutfromtable(tablename,prefix);
		exitseq(0);
	}

	/*** now handle interactive mode ***/

	if (stdoutwrite==1)
	{
		usage("ERROR: Writing to STDOUT in interactive mode is a bad idea.");exitseq(5);
	}

	copytags=0;  /* -c is only for noninteractive mode */

	term_init();
	term_character();

	playfirst(audiobegin);

	while (1)
	{
// 		printf("\n[1..0,r,a,b,s,q] > ");
        printf("\n%3u:%05.2f [1..0,r,a,b,s,q] > ", showmins(startpos-audiobegin), showsecs(startpos-audiobegin));
		c = getchar();
		if (c == 'q') {printf("\n\n");exitseq(0);}
		if (c == '1') {startpos=frewind(startpos,1000*600); play(startpos);}
		if (c == '2') {startpos=frewind(startpos,1000*60); play(startpos);}
		if (c == '3') {startpos=frewind(startpos,1000*10); play(startpos);}
		if (c == '4') {startpos=frewind(startpos,1000); play(startpos);}
		if (c == '5') {startpos=frewind(startpos,100); play(startpos);}
		/* previous frame, when at EOF */
		if (c == ',') {startpos=frewind(startpos,1); play(startpos);}
		if (c == '.') {startpos=fforward(startpos,1); play(startpos);}    /* next frame */
		if (c == '6') {startpos=fforward(startpos,100);play(startpos);}
		if (c == '7') {startpos=fforward(startpos,1000);play(startpos);}
		if (c == '8') {startpos=fforward(startpos,1000*10);play(startpos);}
		if (c == '9') {startpos=fforward(startpos,1000*60);play(startpos);}
		if (c == '0') {startpos=fforward(startpos,1000*600);play(startpos);}
		if (c == 'r') play(startpos);
		if (c == 'R') { play = play == &playsel ? playtoend : playsel; play(startpos);}
		if (c == 'v') printf("  volume=%u",volume(startpos));
		if (c == 's') {savesel(prefix);play=playsel;}
		if (c == 'S') writeconf();
		if (c == 't') savewithtag();
		if (c == 'i') showfileinfo(rawmode,fix_channels);
		if (c == 'h') help();
		if (c == 'N') {howlong=howlong-1;if(howlong<1)howlong=1;printf("  playing %i second(s) now\n",howlong);}
		if (c == 'M') {howlong=howlong+1;printf("  playing %i second(s) now\n",howlong);}
		if (c == 'z') showdebug(startpos,prefix);
		if (c == 'a')
		{
			inpoint=startpos;
			printf("  startpoint set to %u:%05.2f  \n",showmins(inpoint-audiobegin),showsecs(inpoint-audiobegin));
			play=playtoend;
		}
		if (c == 'b')
		{
			outpoint=startpos;
			if(mute)
			printf("  endpoint set to %u:%05.2f  \n",showmins(outpoint-audiobegin),showsecs(outpoint-audiobegin));
			else
			{
			printf("  endpoint set to %u:%05.2f  ",showmins(outpoint-audiobegin),showsecs(outpoint-audiobegin));
// 			playtoend(startpos);
			}
			play=playsel;
		}
		if (c == '#')
		{
			mute=1-mute;
			if (mute) printf("  sound off\n");
			else printf("  sound on\n");
		}
		if (c == 'l')
		{
			livetime=1-livetime;
			if (livetime) printf("  live time on\n");
			else printf("  live time off\n");
		}
		if (c == 'o')
		{
			overwrite=1-overwrite;
			if (1-overwrite) printf("  overwrite off\n");
			else printf("  overwrite on (for next time only)\n");
		}
		if (c == 'p')
		{
			startpos=seeksilence(startpos);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			if (startpos>=dataend) printf("  End of file reached.     \n");
			else
			{
				printf("\n  End of next silence reached.       \n");
				play(startpos);
			}
		}
		if (c == 'P')
		{
			startpos=seeksilstart(startpos);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			if (startpos>=dataend) printf("  End of file reached.     \n");
			else
			{
				printf("\n  End of sound reached.      \n");
				play=playtoend;
				play(startpos);
			}
		}
		if (c == 'T')
		{
			startpos=seektag(startpos);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			if (startpos>=dataend) printf("  End of file reached.     \n");
			else play(startpos);
		}
		if (c == 'W')
		{
			outpoint=startpos;printf("  endpoint set to %u:%05.2f  ",showmins(outpoint),showsecs(outpoint));
			savewithtag();
			inpoint=startpos;printf("  startpoint set to %u:%05.2f  \n",showmins(inpoint),showsecs(inpoint));
		}
		if (c == 'w')
		{
			outpoint=startpos;printf("  endpoint set to %u:%05.2f  \n",showmins(outpoint),showsecs(outpoint));
			savesel(prefix);
			inpoint=startpos;printf("  startpoint set to %u:%05.2f  \n",showmins(inpoint),showsecs(inpoint));
		}
		if (c == 'A')
		{
			startpos=inpoint;
			play(startpos);
		}
		if (c == 'B')
		{
			startpos=outpoint;
			play(startpos);
		}
		if (c == '+')
		{
			silvol=silvol*1.1+1;
			if (silvol>10000) silvol=10000;
			printf("  maximum volume during silence is %u  \n",silvol);
		}
		if (c == '-')
		{
			silvol=silvol/1.1; if (silvol<1) silvol=1;
			printf("  maximum volume during silence is %u  \n",silvol);
		}
		if (c == 'm')
		{
			silencelength=silencelength*1.1;
			if (silencelength>10000 && silfactor !=0) silencelength=10000;
			printf("  length of silence is %u milliseconds \n",silencelength);
		}
		if (c == 'n')
		{
			silencelength=silencelength/1.1;
			if (silencelength<100) silencelength=100;
			printf("  length of silence is %u milliseconds \n",silencelength);
		}
	}
	exitseq(0);
}


/*

MP3-Frame-Header: 4 Bytes

########
0 - 7   Alles 1 f�r Sync
########
8 -10   Alles 1 f�r Sync
11-12   MPEG-Version: 10=MPEG2 11=MPEG1 01=MPEG2.5
13-14   0 1 f�r Layer3
        1 0 f�r Layer2
        1 1 f�r Layer1
   15   Protection Bit, bei 0 soll nach Header Error-Check kommen (CRC)
########
16-19   bitrate encoding
20-21   Sampling Frequency
   22   Padding Bit, if set, frame is 1 byte bigger
   23   Private Bit
########
24-25   Mono/Stereo Modus
26-27   Mode Extension f�r Joint Stereo
   28   Copyright Bit
   29   Original?
30-31   Emphasis
########


------------------------------------------------------------------------------


What you call "volume coefficients" are actually called "global gain
factors". These are stored logarithmically as 8 bit per granule per
channel. A granule is usually half an MP3 frame (in case of 32-48
kHz). These global gain factors are stored in a so-called "side info
block" with a granularity of 1.5 dB.
(global gain factor +/- 4 --> 200% / 50% amplitude)

MP3-Frame
---------
32 Bit Header
16 Bit CRC (optional)
xx Bytes Side Info Block
yy Bytes Main Data

where xx =
9  in case of MPEG2/2.5 mono
17 in case of MPEG2/2.5 stereo or MPEG1 mono
32 in case of MPEG1 stereo

For more details see
www.mp3-tech.org -> programmer's corner -> technical audio papers
-> ISO 11172 Coding of [...] Part 3: Audio
This is a draft of the MPEG-1 audio Layer 1/2/3 specification.

Note: These global gain factors are NOT 8bit-aligned.

*/
