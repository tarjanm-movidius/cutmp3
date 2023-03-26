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
	(c) Jochen Puchalla 2003-2015  <mail@puchalla-online.de>
*/

#include "cutmp3.h"

#define VERSION "3.0"
#define YEAR "2015"

/* general buffersize */
#define BUFFER 32000

/* 10 MB for VBR scanning */
#define SCANAREA 10485759

	int debug=0; // 1=volume 5=ID3tags 7=saving+cutting 9=seeksilence

	unsigned int silencelength=1000, silvol=1;

	FILE *mp3file, *timefile;
	unsigned char begin[BUFFER];
	long filesize, dataend, inpoint, outpoint;
	int audiobegin=0;
	int startmins, endmins;
	double startsecs, endsecs;
	int negstart=1, negend=1; /* start- or endpoint counted from end? */
	int totalmins; /* minutes of total time */
	double totalsecs; /* seconds of total time (<60) */
	int howlong=1; /* play how many seconds? */
	int fix_secondbyte=0, fix_sampfreq=0, fix_channelmode=7, fix_mpeg=7, fix_frsize=1;
	double fix_frametime=0;
	int vbr=0, a_b_used=0;
	double avbr, msec; /* average bitrate and one millisecond in relation to avbr */
	long totalframes;
	int usefilename=0;

	char *filename;
	char *forcedname;
	char *userin;
	char *userout;

	int livetime=1;
	int mute=0, forced_file=0, overwrite=0, forced_prefix=0;
	int copytags=0, nonint=0;
	long silfactor=15; /* silence already longer than wanted by this factor */
	int stdoutwrite=0;
	long framenumber=0, framecount=0;
	double intime=0;   /* time of inpoint */
	double outtime=0;  /* time of outpoint */
	double endtime=0;  /* total time of MP3 in milliseconds */
	int no_tags=0;

	int card=1;
	signed int genre=-1;

	long framepos1000[1000000]; // byte position of every 1000th frame
	long id3v2[1000000]; // space to copy id3v2 tag
	long id3v1[128]; // space to copy id3v2 tag
	int minorv=0; // id3v2 minor version, mostly 3, rarely 4, 2 is obsolete

	char artist[255]="";
	char album[255]="";
	char year[255]="";
	char track[255]="";
	char comment[255]="";
	/* title is used to determine whether we name the file "result000x.mp3"
	or "artist - title.mp3", so leave it empty here! */
	char title[255]="";

	int hastable=0;
	unsigned char ttable[BUFFER];
	FILE *tblfile;
	char tblname[17] = "/tmp/tableXXXXXX";

	FILE *logfile;
	char logname[21] = "/tmp/mpg123logXXXXXX";
	int fdlog=-1;

void exitseq(int foobar)
{
 	remove(logname);

	exit(foobar);  /* now really exit */
}

void usage(char *text)
{
	printf("\ncutmp3  version %s  \n", VERSION);
	printf("\nUsage:  cutmp3 -i file.mp3 [-a inpoint] [-b outpoint] [-f timetable]");
	printf("\n               [-o outputprefix] [-c] [-C] [-k] [-q]\n");
	printf("\n  cutmp3 -i file.mp3 -a 0:37 -b 3:57  copies valid data from 0:37 to 3:57");
	printf("\n  cutmp3 -i file.mp3 -f timetable     copies valid data described in timetable");
	printf("\n  cutmp3 -i file.mp3 -o song          writes the output to song0001.mp3, song0002.mp3,...");
	printf("\n  cutmp3 -i file.mp3 -k               appends 0001.mp3, etc. to original filename");
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


int bitrate(unsigned char secondbyte,unsigned char thirdbyte,unsigned char fourthbyte)
{
	unsigned char bitratenumber;

	bitratenumber = thirdbyte>>4;

	if (layer(secondbyte)==3) /* MPEG Layer3 */
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

	if (layer(secondbyte)==2) /* MPEG Layer2 */
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

	if (layer(secondbyte)==1) /* MPEG Layer1 */
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

	if (seekpos>=filesize) return filesize;

	fseek(mp3file, seekpos, SEEK_SET);

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
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			if (framesize(b,c,d)!=1 && sampfreq(b,c)!=1) break;  /* next header found */
		}
		seekpos++;
	}

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
	}

	if (seekpos<audiobegin) seekpos=audiobegin;
	return seekpos;
}


void zaptitle (void)
{
	title[0]='\0';
}


/*
time of frame in milliseconds is: framesize*8/bitrate == 1152000/sampfreq !
because bitrate=128 means 128000bits/s
*/


/* scan whole file, can take seconds */
void scanframes()
{
	long pos=0,count=0,i=0;

	pos=audiobegin; {framepos1000[i]=pos; i++;}
	while (pos<filesize)
	{
		pos=nextframe(pos);
		count++;
		if ( (double)count/(double)1000 == count/1000 ) {framepos1000[i]=pos; i++;}
	}
	totalframes=count;
	framepos1000[i]=filesize+1;
	return;
}


/* get frame number from position, quite slow */
long pos2frame (long seekpos)
{
	long pos;
	long count=0,i=0;

	if (seekpos<=audiobegin) return 0;
	if (seekpos>dataend) return totalframes;

	pos=audiobegin;

	while (pos>0 && pos<seekpos) // go forward until too far
	{
		i++;
		pos=framepos1000[i];
	}

	while (pos>=seekpos) // go back to one before
	{
		i--;
		pos=framepos1000[i];
	}

	count=1000*i; // we know at how many thousands of frames we are

	while (pos<seekpos) // find exact frame
	{
		pos=nextframe(pos);
// 		printf(".");
		count++;
	}

	return count;
}


/* get time from position, quite slow */
double pos2time (long seekpos)
{
	long framenr;

	framenr=pos2frame(seekpos);
	return (double)framenr*fix_frametime;
}


/* get whole minutes from position, quite slow */
int pos2mins (long seekpos)
{
	long framenr;

	framenr=pos2frame(seekpos);
	return (double)framenr*fix_frametime/60000;
}


/* returns seconds without minutes to display mm:ss.ss, quite slow */
double pos2secs (long seekpos)
{
	long framenr;
	double msecs,temp;
	int mins;

	framenr=pos2frame(seekpos);
	msecs=(double)framenr*fix_frametime;
	mins=msecs/60000;
	temp=(msecs/1000-(mins*60));
	if (temp>59.99) return 59.99; /* 59.997 would be displayed as 60:00 without this */
	return temp;
}


/* time in milliseconds from frame number, fast! */
double frame2time (long framenr)
{
	if (framenr>totalframes) framenr=totalframes;

	return (double)framenr*fix_frametime;
}


/* whole minutes from frame number, fast! */
int frame2mins (long framenr)
{
	return frame2time(framenr)/(double)60000;
}


/* seconds over whole minutes from frame number, fast! */
double frame2secs (long framenr)
{
	double temp;

	temp=frame2time(framenr)/1000 - frame2mins(framenr)*60;
	if (temp>59.99) return 59.99; /* 59.997 would be displayed as 60:00 without this */
	return temp;
}


/* position from frame number, quite fast */
long frame2pos (long framenr)
{
	long pos,i;
	int tsd;

	tsd=framenr/1000; /* start search at 1000s frames */
// 	printf(" tsd=%i \n",tsd);
	pos=framepos1000[tsd];
	for (i=0;i<framenr-tsd*1000;i++) pos=nextframe(pos);

	return pos;
}


long fforward (long seekpos,long skiptime)
{
	long skipframes=0;

	if (frame2time(framenumber)+skiptime>=endtime)
	{
		framenumber=totalframes;
		return filesize;
	}

// 	go forward skiptime/fix_frametime +1 headers
	skipframes=(double)skiptime/(double)fix_frametime+(double)1;
	framenumber=framenumber+skipframes;

	return frame2pos(framenumber);
}


long frewind (long seekpos,long rewtime)
{
	long rewframes=0;

	if (rewtime>=frame2time(framenumber))
	{
		framenumber=0;
		return audiobegin;
	}

// 	go back rewtime/fix_frametime +1 headers
	rewframes=(double)rewtime/(double)fix_frametime+(double)1;
	framenumber=framenumber-rewframes;

	return frame2pos(framenumber);
}


/* Determination of average VBR bitrate */
double avbitrate(void)
{
	double framecount=0, bitratesum=0;
	long pos=audiobegin;
	unsigned char a,b,c,d;
	double thisfrsize=0, thisbitrate=0;

//	printf("  avbitrate");
	fseek(mp3file, audiobegin, SEEK_SET);
	while(1)                              /* loop till break */
	{
//		printf("\navbitrate in loop, pos=%ld, filesize=%ld\n",pos,filesize);
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
//				printf("\nbitratesum:%ld framecount:%ld ",bitratesum,framecount);
				if (bitratesum/framecount!=thisbitrate && vbr==0)    /* VBR check */
				{
					vbr=1;     /* if file is VBR */
//					printf("\nnoVBR: bitrate=%.0f at %ld Bytes \n",thisbitrate,pos);
				}
			}
			else fseek(mp3file, pos, SEEK_SET);    /* rewind to next byte for if (a==255) */
		}
	}

	if (bitratesum==0 || framecount==0) return 0;
	return bitratesum/framecount;
}


/* This function shall return the volume (energy level)
   of one frame. Taken from mpglib and mpcut. */
unsigned int volume(long playpos)
{
	unsigned char a,b,c,d;
	unsigned int level, framenum=4;
	int i, frsize;
	long pos;

	FILE *volfile;
	char volname[15] = "/tmp/volXXXXXX";
	int fd=-1;

	if (debug==1) printf("\nvolume(): before prevframe(playpos)");

	for (i=0; i<framenum ; i++) playpos=prevframe(playpos);  /* step back framenum frames */

	if (debug==1) printf("\nvolume(): after %ux prevframe %ld \n",framenum,playpos);

	if ((fd = mkstemp(volname)) == -1 || (NULL== (volfile = fdopen(fd, "w+b"))) )
	{
		perror("\ncutmp3: failed writing temporary volume mp3 in /tmp");exitseq(6); /* open temp.vol.file read-writable zero length */
	}

	frsize=1;
	pos=playpos-1;

	/* Copy 2*framenum valid frames to volfile */
	for (framecount=0; framecount < 2*framenum ; framecount++)
	{
		while (frsize==1)
		{
			/* seek next frame */
			pos=nextframe(pos);
			if (debug==1) printf("\nvolume(): pos=%ld\n",pos);
			fseek(mp3file, pos, SEEK_SET);
			a=fgetc(mp3file);
			b=fgetc(mp3file);
			c=fgetc(mp3file);
			d=fgetc(mp3file);
			frsize=framesize(b,c,d);
		}
		if (debug==1) printf("\nvolume(): frsize=%u\n",frsize);
		/* write this frame */
		fseek(mp3file, pos, SEEK_SET);
		for (i=0 ; i<frsize && pos+frsize<filesize ; i++)
		{
			fputc(fgetc(mp3file),volfile);
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


/* This function returns the position of the end of the next silence */
long seeksilence(long seekpos)
{
	unsigned int vol;
	long silend, silfirstfound;

//	printf("  seeksilence()");

	seekpos=fforward(seekpos,silencelength); /* move on */
	if (seekpos>=dataend) /* dataend reached? */
	{
		printf("  seeking at        ");
		framenumber=totalframes;
		printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));
		return filesize;
	}

	printf("  seeking at        ");
	printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));

	while (seekpos<dataend)
	{
		while ((vol=volume(seekpos)) >= silvol)
		{
			/* look for possible silence every silencelength milliseconds: */
			if (debug==9) printf("\ncandidate seek at %ld   volume is %u\n",seekpos,vol);
			printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));
			seekpos=fforward(seekpos,silencelength);
 			framenumber=pos2frame(seekpos);
			if (seekpos>=dataend)
			{
				return filesize; /* dataend reached? */
			}
		}

		/* we found a candidate! */
		if (debug==9) printf("\ncandidate found at %ld   volume is %u.\n",seekpos,vol);
// 		printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber)); unnötig;
		silfirstfound=seekpos; /* remember first found silent frame */
		framenumber=pos2frame(seekpos);

		/* forward to last silent frame */
		while ((vol=volume(seekpos)) < silvol && seekpos<dataend)
		{
			seekpos=nextframe(seekpos); framenumber++;
			printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));
			if (debug==9) printf("\nforward seek1 at %ld   volume is %u\n",seekpos,vol);
		}
		silend=seekpos;
		if (debug==9) printf("\nsilend at %ld   \n",silend);

		/* quick check if silence is already long enough */
		if (pos2time(silend)-pos2time(silfirstfound) >= silencelength)
		{
			framenumber--;
			return frame2pos(framenumber);
		}

		/* rewind silencelength ms */
		seekpos=frewind(seekpos,silencelength);
		framenumber=pos2frame(seekpos);
		if (debug==9) printf("\nrewind to %ld   \n",seekpos);

		/* forward to last silent frame again and see if position is silend */
		while ((vol=volume(seekpos)) < silvol && seekpos<dataend)
		{
			seekpos=nextframe(seekpos); framenumber++;
			if (debug==9) printf("\nforward seek2 at %ld   volume is %u\n",seekpos,vol);
		}
		if (debug==9) printf("\nafter seek2 at %ld   volume is %u\n",seekpos,vol);
		if (seekpos==silend) /* we found a silence */
		{
			framenumber--;
			return frame2pos(framenumber);
		}
		else
		{
			framenumber=pos2frame(silend);
			seekpos=fforward(silend,silencelength);
			if (debug==9) printf("\nfforward to %ld   \n",seekpos);
			if (debug==9) printf("\nsilend at %ld   \n",silend);
		}
	}
	framenumber=totalframes;
	return filesize; /* EOF */
}


/* This function returns the position of the beginning of the next silence */
long seeksilstart(long seekpos)
{
	unsigned int vol;
	long silstart, silend, silfirstfound;
	long silfirstfoundframe;

	seekpos=fforward(seekpos,silencelength); /* move on */
	if (seekpos>=dataend) /* dataend reached? */
	{
		printf("  seeking at        ");
		framenumber=totalframes;
		printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));
		return filesize;
	}

	printf("  seeking at        ");
	printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));

	while (seekpos<dataend)
	{
		while ((vol=volume(seekpos)) >= silvol)
		{
			/* look for possible silence every silencelength milliseconds: */
			if (debug==9) printf("\ncandidate seek at %ld   volume is %u\n",seekpos,vol);
			printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));
			seekpos=fforward(seekpos,silencelength);
 			framenumber=pos2frame(seekpos);
			if (seekpos>=dataend)
			{
				return filesize; /* dataend reached? */
			}
		}

		/* we found a candidate! */
		if (debug==9) printf("\ncandidate found at %ld   volume is %u.\n",seekpos,vol);
// 		printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber)); unnötig;
		silfirstfound=seekpos; /* remember first found silent frame */
		framenumber=pos2frame(seekpos);
		silfirstfoundframe=framenumber; /* remember first found silent frame number */

		/* forward to last silent frame */
		while ((vol=volume(seekpos)) < silvol && seekpos<dataend)
		{
			seekpos=nextframe(seekpos); framenumber++;
			if (debug==9) printf("\nforward seek at %ld   volume is %u\n",seekpos,vol);
			/* quick check if silence is already long enough */
			if (framenumber-silfirstfoundframe >= silencelength/fix_frametime)
			{
				framenumber=silfirstfoundframe+1;
				return frame2pos(framenumber);
			}
		}
		silend=seekpos;
		if (debug==9) printf("\nsilend at %ld   \n",silend);

		/* rewind to first silent frame from first found silent frame */
		seekpos=silfirstfound;
		framenumber=pos2frame(silfirstfound);
		if (debug==9) printf("\nrewind to %ld   \n",seekpos);
		while ((vol=volume(seekpos)) < silvol && seekpos>audiobegin)
		{
			seekpos=prevframe(seekpos); framenumber--;
			printf("\b\b\b\b\b\b\b%4u:%02.0f",frame2mins(framenumber),frame2secs(framenumber));
			if (debug==9) printf("\nbackward seek at %ld   volume is %u\n",seekpos,vol);
		}
		silstart=seekpos;
		if (debug==9) printf("\nsilend at %ld   \n",silend);

		if (pos2time(silend)-pos2time(silstart) >= silencelength)
		{
			if (debug==9) printf("\nseeksilstart(): silend=%ld silstart=%ld\n",silend,silstart);
			framenumber++;
			return frame2pos(framenumber);
		}
		/* silence was not long enough! */

		/* Do not find the same silence again! Jump to end of silence and seek on */
		seekpos=nextframe(silend);
		framenumber=pos2frame(seekpos);
		if (debug==9) printf("\nseekpos=%ld dataend=%ld\n",seekpos,dataend);
	}
	framenumber=totalframes;
	return filesize; /* EOF */
}


/* This function prints the reached ID3 V2 TAG */
void print_id3v2(long showpos)
{
	if (importid3v2(showpos)==0) return;
	printf("\n\nFound ID3 V2 tag of this next track:");
	printf("\nTitle:   %s",title);
	printf("\nArtist:  %s",artist);
	printf("\nAlbum:   %s",album);
	printf("\nYear:    %s",year);
	printf("\nComment: %s\n",comment);
	return;
}


/* This function prints the reached ID3 V1 TAG */
void print_id3v1(long seekpos)
{
	if (importid3v1(seekpos)==0) return;

	printf("\nID3 V1 tag:");
	printf("\nTitle:   %s",title);
	printf("\nArtist:  %s",artist);
	printf("\nAlbum:   %s",album);
	printf("\nYear:    %s",year);
	printf("\nComment: %s\n",comment);
	return;
}

/* This function prints the reached ID3 V1 TAG,
   argument is next byte after tag */
void showtag(long seekpos)
{
	if (importid3v1(seekpos)==0) return;

	printf("\n\nReached end of this track:   ");
	printf("\nTitle:   %s",title);
	printf("\nArtist:  %s",artist);
	printf("\nAlbum:   %s",album);
	printf("\nYear:    %s",year);
	printf("\nComment: %s",comment);
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


/*
Textual frames are marked with an encoding byte.[7]

$00 – ISO-8859-1 (LATIN-1, Identical to ASCII for values smaller than 0x80).
$01 – UCS-2 (UTF-16 encoded Unicode with BOM), in ID3v2.2 and ID3v2.3.
$02 – UTF-16BE encoded Unicode without BOM, in ID3v2.4.
$03 – UTF-8 encoded Unicode, in ID3v2.4.
(BOM= byte order mark: 255 254 or 254 255)
*/


/* This function gets data from ID3 V2 and returns its length,
   argument is start of tag */
long importid3v2(long seekpos)
{
	long length=0, pos=0, i=0, j=0, bytesin=0;
	int size=0, hasexthdr=0, exthdrsize=0, footerlength=0;
	unsigned char a,b,c,d;
	int revision, encoding=0;
	int ssf=1; // synch safe factor

	/* delete old stuff in case they are not overwritten: */
	title[0]='\0';
	year[0]='\0';
	album[0]='\0';
	artist[0]='\0';
	comment[0]='\0';

	if (debug==5){printf("\n *debug* importid3v2 at %ld\n",seekpos);}

	fseek(mp3file, seekpos, SEEK_SET);
	length=skipid3v2(seekpos)-seekpos; /* skipid3v2 also checks if there is ID3 V2 */
	if (debug==5){printf(" *debug* id3v2 length %ld \n",length);}
	if (length==0) return 0; /* is not ID3 V2 */

	/* copy whole tag to buffer, max 1E6 Bytes */
	fseek(mp3file, seekpos, SEEK_SET);
	for (bytesin=0 ; bytesin < length && bytesin<1E6 ; bytesin++) id3v2[bytesin]=fgetc(mp3file);

	minorv=id3v2[3]; if (minorv==3) ssf=2;
	revision=id3v2[4];
	if (debug==5){printf("\n *debug* found ID3 version 2.%u.%u\n",minorv,revision);}
	a=b=id3v2[5]; /* flags byte */
	if (debug==5){printf(" *debug* flags byte=%u ",a);}

	if (minorv>=3) hasexthdr=(a<<1)>>7; /* has extended header? */
	if (minorv==4) footerlength=((b<<3)>>7)*10; /* has footer? */
							/* skip 4 bytes tag length info */

	if (hasexthdr)
	{
		/* skip ext header */
		a=id3v2[10];
		b=id3v2[11];
		c=id3v2[12];
		d=id3v2[13];
		pos=pos+4;
		exthdrsize=a*128*128*128+b*128*128+c*128+d;
		if (debug==5){printf("\n *debug* hasexthdr of size=%i\n",exthdrsize);}
	}
	pos=10+exthdrsize;

	if (minorv==3 || minorv==4) /* ID3V2.3 & ID3V2.4 */
	{
		while (pos<length)
		{
			if (debug==5){printf("\n *debug* loopstart pos=%ld length=%ld footerlength=%i\n",pos,length,footerlength);}
			if (pos+4>=length-footerlength) break;

			while ((id3v2[pos]<48 || id3v2[pos]>90) && pos<length)
			{
				pos++;
			}
			if (debug==5){printf(" *debug* after alphanum pos=%ld\n",pos);}

			a=id3v2[pos];
			b=id3v2[pos+1];
			c=id3v2[pos+2];
			d=id3v2[pos+3];
			pos=pos+4;

			if (debug==5){printf("\n *debug* loopstart: %c%c%c%c\n",a,b,c,d);}
			if (debug==5){printf("\n *debug* loopstart: a=%u b=%u c=%u d=%u\n",a,b,c,d);}


			if (a=='T' && b=='I' && c=='T' && d=='2') /* read title */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* also skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				encoding=id3v2[pos]; pos++; size=size-1;
				if (encoding==1){pos=pos+2; size=size-2;}
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					title[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				title[i]='\0';
				pos=pos+size;
				if (debug==5){printf(" *debug* Title: %s\n",title);}
			}
			else if (a=='T' && b=='P' && c=='E' && d=='1') /* read artist */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* also skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				encoding=id3v2[pos]; pos++; size=size-1;
				if (encoding==1){pos=pos+2; size=size-2;}
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					artist[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				artist[i]='\0';
				pos=pos+size;
				if (debug==5){printf(" *debug* Artist: %s\n",artist);}
			}
			else if (a=='T' && b=='Y' && c=='E' && d=='R') /* read year */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* also skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				encoding=id3v2[pos]; pos++; size=size-1;
				if (encoding==1){pos=pos+2; size=size-2;}
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					year[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				year[i]='\0';
				pos=pos+size;
				if (debug==5){printf(" *debug* Year: %s\n",year);}
			}
			else if (a=='T' && b=='D' && c=='R' && d=='C') /* read year in 2.4.0 */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* also skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				encoding=id3v2[pos]; pos++; size=size-1;
				if (encoding==1){pos=pos+2; size=size-2;}
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					year[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				year[i]='\0';
				pos=pos+size;
				if (debug==5){printf(" *debug* Year: %s\n",year);}
			}
			else if (a=='T' && b=='A' && c=='L' && d=='B') /* read album */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* also skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				encoding=id3v2[pos]; pos++; size=size-1;
				if (encoding==1){pos=pos+2; size=size-2;}
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					album[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				album[i]='\0';
				pos=pos+size;
				if (debug==5){printf(" *debug* Album: %s\n",album);}
			}
			else if (a=='T' && b=='R' && c=='C' && d=='K') /* read track */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* also skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				encoding=id3v2[pos]; pos++; size=size-1;
				if (encoding==1){pos=pos+2; size=size-2;}
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					track[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				track[i]='\0';
				pos=pos+size;
				if (debug==5){printf(" *debug* Track: %s\n",track);}
			}
			else if (a=='C' && b=='O' && c=='M' && d=='M') /* read comment */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* also skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				encoding=id3v2[pos]; pos++; size=size-1;
				pos=pos+3; size=size-3;// skip language info
				while ((id3v2[pos]<32 || id3v2[pos]>253) && pos<length){pos++; size--;} // skip no-text-chars
				if (debug==5){printf(" *debug* frame size %d pos=%ld\n",size,pos);}
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					comment[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				comment[i]='\0';
				pos=pos+size;
				if (debug==5){printf(" *debug* Comment: %s\n",comment);}
			}
			else if (a>=48 && a<=90 && b>=48 && b<=90 && c>=48 && c<=90 && d>=48 && d<=90)
			{
				/* frame not important, so skip it */
				if (debug==5){printf("\n *debug* skipped %c%c%c%c\n",a,b,c,d);}
// 				{printf("\n* ignored ID3V2 frame named %c%c%c%c",a,b,c,d);}
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				d=id3v2[pos+3];
				pos=pos+6; /* skip 2 flag bytes */
				size=a*128*ssf*128*ssf*128*ssf+b*128*ssf*128*ssf+c*128*ssf+d;
				if (debug==5){printf("\n *debug* skip size=%d length=%ld\n",size,length);}
				if (pos+size>length) break;
				pos=pos+size;
				if (debug==5){printf("\n *debug* next pos=%ld \n",pos);}
			}
		}
	}

	if (minorv==2) /* ID3V2.2 */
	{
		while (pos<length)
		{
			if (pos+3>=length) break;
			a=id3v2[pos];
			b=id3v2[pos+1];
			c=id3v2[pos+2];
			pos=pos+3;

			if (a==0 && b==0 && c==0) break; /* is padding at the end of the tag */

			else if (a=='T' && b=='T' && c=='2') /* read title */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				pos=pos+3;
				size=a*256*256+b*256+c;
				if (id3v2[pos]==0){encoding=0;}
				if (id3v2[pos]==1){encoding=1;}
				while ((id3v2[pos]<32 || id3v2[pos]>253) && pos<length){pos++; size--;} // skip no-text-chars
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					title[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				pos=pos+size;
			}
			else if (a=='T' && b=='P' && c=='1') /* read artist */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				pos=pos+3;
				size=a*256*256+b*256+c;
				if (id3v2[pos]==0){encoding=0;}
				if (id3v2[pos]==1){encoding=1;}
				while ((id3v2[pos]<32 || id3v2[pos]>253) && pos<length){pos++; size--;} // skip no-text-chars
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					artist[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				pos=pos+size;
			}
			else if (a=='T' && b=='Y' && c=='E') /* read year */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				pos=pos+3;
				size=a*256*256+b*256+c;
				if (id3v2[pos]==0){encoding=0;}
				if (id3v2[pos]==1){encoding=1;}
				while ((id3v2[pos]<32 || id3v2[pos]>253) && pos<length){pos++; size--;} // skip no-text-chars
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					year[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				pos=pos+size;
			}
			else if (a=='C' && b=='O' && c=='M') /* read comment */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				pos=pos+3;
				size=a*256*256+b*256+c;
				if (id3v2[pos]==0){encoding=0;}
				if (id3v2[pos]==1){encoding=1;}
				while ((id3v2[pos]<32 || id3v2[pos]>253) && pos<length){pos++; size--;} // skip no-text-chars
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					comment[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				pos=pos+size;
			}
			else if (a=='T' && b=='A' && c=='L') /* read album */
			{
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				pos=pos+3;
				size=a*256*256+b*256+c;
				if (id3v2[pos]==0){encoding=0;}
				if (id3v2[pos]==1){encoding=1;}
				while ((id3v2[pos]<32 || id3v2[pos]>253) && pos<length){pos++; size--;} // skip no-text-chars
				i=0;
				for (j=pos; j<pos+size && i<255; j++)
				{
					album[i]=id3v2[j]; i++;
					if (encoding==1){j++;}
				}
				pos=pos+size;
			}
			else if (a>=48 && a<=90 && b>=48 && b<=90 && c>=48 && c<=90 && d>=48 && d<=90)
			{
				/* frame not important, so skip it */
				a=id3v2[pos];
				b=id3v2[pos+1];
				c=id3v2[pos+2];
				pos=pos+3;
				size=a*256*256+b*256+c;
				if (pos+size>length) break;
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

	Header:
	3 Bytes ID3
	2 Bytes version (04 00 for 2.4.0)
	1 Byte flags
	4 Bytes length (0 + 7 bits, each)

	Frames:
	TIT2=title
	TPE1=artist
	TYER=year
	COMM=comment
	TALB=album
	*/

	/*
	Main info in V2.2:

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


/* This function gets data from ID3 V1 and returns its length,
   argument is end of tag */
int importid3v1(long seekpos)
{
	int j=0;

//	printf("\nimportid3v1 at %ld\n",seekpos);

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

	/* copy tag to buffer */
	fseek(mp3file, seekpos-128, SEEK_SET); /* reset to right position */
	for (j=0;j<128;j++) {id3v1[j]=fgetc(mp3file);}

	/* in case there was no info about them: */
	if (strlen(title)==0) snprintf(title,31,"%s","unknown title");
	if (strlen(artist)==0) snprintf(artist,31,"%s","unknown artist");

	return 128;
}


/* This function skips ID3 V1 tag, if any.
   Returns position of next byte after tag. */
long skipid3v1(long seekpos)
{
	unsigned char a,b,c,d;

	if (seekpos+4>filesize) return seekpos;

	fseek(mp3file, seekpos, SEEK_SET);
	a=fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);
	d=fgetc(mp3file);

	/* check for ID3 V1, this is quite safe */
	if (a=='T' && b=='A' && c=='G') return seekpos+128;

	else /* check for ID3 V1 after this frame */
	{
		if (seekpos+framesize(b,c,d)+4>filesize) return seekpos;
		fseek(mp3file, seekpos+framesize(b,c,d), SEEK_SET);
		a=fgetc(mp3file);
		b=fgetc(mp3file);
		c=fgetc(mp3file);
		if (a=='T' && b=='A' && c=='G')	return seekpos+framesize(b,c,d)+128;
	}
	return seekpos;
}


/* This function rewinds to before ID3V1 tag, if any.
   Returns position of last byte before tag. */
long rewind3v1(long seekpos)
{
	unsigned char a,b,c;

	if (seekpos-128<0) return seekpos;

	fseek(mp3file, seekpos-128, SEEK_SET);
	a=fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);

	/* check for ID3 V1, this is quite safe */
	if (a=='T' && b=='A' && c=='G')
	{
// 		printf("\nID3 V1 found at %ld\n",seekpos);
		seekpos=seekpos-128;
	}
	return seekpos;
}


/* This function rewinds max 100kB to before ID3V2 tag, if any.
   Returns position of first byte of tag. */
long rewind3v2(long seekpos)
{
	long oldseekpos=seekpos;   /* remember seekpos */
	unsigned char a,b,c;
	long id3pos,skippedpos;

	while (seekpos>0)
	{
		fseek(mp3file, seekpos, SEEK_SET); /* (re)set to right position */
		while ((a=fgetc(mp3file))!='I' && seekpos>oldseekpos-1E5)
		 {seekpos--; fseek(mp3file, seekpos, SEEK_SET);} /* look for 'I' */
		b=fgetc(mp3file);
		c=fgetc(mp3file);
		if (a=='I' && b=='D' && c=='3' ) /* ID3 is there */
		{
			id3pos=seekpos;
			if ( (skippedpos=skipid3v2(id3pos)) != id3pos )  /* ID3 is valid */
			{
				return id3pos; /* return position of ID3 */
			}
		}
		else seekpos--;
	}
	return oldseekpos;
}


/* This function skips ID3 V2 tag, if any.
   Useful also for getting the length of the ID3 V2 tag.
   Returns position of next byte after tag. */
long skipid3v2(long seekpos)
{
	long oldseekpos=seekpos;   /* remember seekpos */
	unsigned char buffer[15];
	int pos, footer, length;

	if (seekpos+10>filesize) return seekpos;

	fseek(mp3file, seekpos, SEEK_SET);

	/* Load 10 Bytes into unsigned buffer[] */
	for (pos=0 ; pos<10 && pos<=filesize ; pos++) buffer[pos]=fgetc(mp3file);

	/* check for ID3 V2.(<5).(<10), this is quite safe */
	if (buffer[0]=='I' && buffer[1]=='D' && buffer[2]=='3' && buffer[3]<5 && buffer[4]<10 && (buffer[5]<<4)==0 && buffer[6]<128 && buffer[7]<128 && buffer[8]<128 && buffer[9]<128)
	{
// 		printf("\nID3 V2 found at %ld\n",seekpos);
		footer=buffer[5]; footer=footer<<3; footer=footer>>7; footer=footer*10; /* check for footer presence */
		length=buffer[6]*128*128*128+buffer[7]*128*128+buffer[8]*128+buffer[9];
		seekpos=seekpos+10+length+footer;
	}
	fseek(mp3file, oldseekpos, SEEK_SET); /* really necessary? */
// 	printf("\nID3 V2 length is %ld\n",seekpos);
	return seekpos;

	/*
	buffer[3] = v2 minor version (2.X.0)
	buffer[4] = v2 revision number (2.4.X)
	buffer[5] = flags byte, 4 lowest bits must be zero
	buffer[6-9] = synchsafe, highest bit is zero
	*/
}


/* This function returns the position of the next ID3 TAG,
   next byte after V1 and first byte of prepended V2 */
long seektag(long seekpos)
{
	unsigned char a,b,c,tag=0;
	long skippedpos, id3pos;

//	printf("%ld\n",seekpos);
	printf("  seeking ...       ");

	seekpos++; /* do not find ID3 again */

	while (seekpos<filesize)
	{
		fseek(mp3file, seekpos, SEEK_SET); /* (re)set to right position */
		while ((a=fgetc(mp3file))!='T' && a!='I' && seekpos<filesize) seekpos++; /* look for 'T' or 'I' */
		seekpos++;
// 		printf("\b\b\b\b\b\b\b%4u:%02.0f",pos2mins(seekpos),pos2secs(seekpos)); too slow!
		b=fgetc(mp3file);
		c=fgetc(mp3file);
		if (a=='T' && b=='A' && c=='G' ) /* TAG is there */
		{
			if (nextframe(seekpos) == seekpos+127) tag=1; /* TAG is valid, next frame is OK */
			if (skipid3v2(seekpos+127) != seekpos+127) tag=2; /* TAG is valid, ID3 is next */
			if (tag>0)
			{
				/* skip TAG and return position */
				showtag(seekpos+127);
				if (tag==2) print_id3v2(seekpos+127); /* Also show next track info */
				printf("\n\n[1..0,r,a,b,s,q] >                      ");
				framenumber=pos2frame(seekpos-1);
				return seekpos+127;
			}
		}
		if (a=='I' && b=='D' && c=='3' ) /* ID3 is there */
		{
			id3pos=seekpos-1;
			if ( (skippedpos=skipid3v2(id3pos)) != id3pos )  /* ID3 is valid */
			{
				tag=1;
				fseek(mp3file, skippedpos, SEEK_SET); /* Now check if TAG is next */
				a=fgetc(mp3file);
				b=fgetc(mp3file);
				c=fgetc(mp3file);
				if (a=='T' && b=='A' && c=='G' ) /* ID3 was an appended tag, because TAG is next. */
				{
					showtag(skippedpos+128);
					printf("\n\n[1..0,r,a,b,s,q] >                      ");
					/* Skip TAG and return position */
					framenumber=pos2frame(seekpos);
					return seekpos+128;
				}
				else /* ID3 is a prepended tag */
				{
					print_id3v2(id3pos);
					printf("\n[1..0,r,a,b,s,q] >                      ");
					framenumber=pos2frame(id3pos);
					return id3pos; /* return position of ID3 */
				}
			}
		}
	}
	framenumber=totalframes;
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
void playsel(long playpos)
{
	long bytesin;
	long playsize;
	int skipframes=(double)howlong*(double)1000/fix_frametime; /* real time */
	long pos;
	FILE *outfile;
	char command[1023];
	char playname[16] = "/tmp/playXXXXXX";
	static char prev_playname[16] = "";
	int fd=-1;
	int i=0;
	long tempfrnr=pos2frame(playpos);
	int mins,secs;

	if (prev_playname[0] != 0) {
		// printf("remove file %s\n", prev_playname); /* debug */
		remove(prev_playname); /* delete tempfile */
	}

	/* Determine playsize: */
	pos=playpos;
	for (i=0;i<skipframes;i++){pos=nextframe(pos);}
	playsize=pos-playpos;

	if (!mute)
	{
		if (playsize < 0) playsize=0;
		if (playpos < audiobegin)
		{
// 			playsize=playsize-(audiobegin-playpos);
			playpos=audiobegin;
		}
		if (playpos > filesize) playpos=filesize;
		if ((fd = mkstemp(playname)) == -1 || (NULL== (outfile = fdopen(fd, "w+b"))) )
		{
			perror("\ncutmp3: failed writing temporary mp3 in /tmp");exitseq(9);
		} else {
			strncpy(prev_playname, playname, 16);
		}
		fseek(mp3file, playpos, SEEK_SET);
		for (bytesin=0; bytesin < playsize; bytesin++) fputc(getc(mp3file),outfile);
		fclose(outfile);
	}

	pos=playpos;

	if (!mute)
	{
 		if (card > 1)
		snprintf(command,1022," %s %s %s%u %s %s %s","mpg123",playname,"-a /dev/dsp",card-1,">",logname,"2>&1 &");
		else
		snprintf(command,1022," %s %s %s %s %s","mpg123",playname,">",logname,"2>&1 &");
		system(command);  /* play mp3 excerpt */

// 		printf("%s\n",command); /* debug */

		printf("  playing at %4u:%05.2f",pos2mins(pos),pos2secs(pos));

		/* show live time? */
		if (livetime==1)
		{
			i=0; while (i<howlong*20)
			{
 				printf("\b\b\b\b\b\b\b\b\b\b\b %4u:%05.2f",frame2mins(tempfrnr),frame2secs(tempfrnr));
				usleep(50000);
				tempfrnr=(double)tempfrnr+(double)50/(double)fix_frametime+1;
				if (tempfrnr>totalframes) break;
				i++;
			}
		}
		printf(" / %u:%05.2f  ",totalmins,totalsecs);
	}
	else
	{
 		printf("  position is %4u:%05.2f / ",frame2mins(framenumber),frame2secs(framenumber));
		printf("%u:%05.2f  ",totalmins,totalsecs);
	}

	secs=abs(pos2time(inpoint)-frame2time(framenumber))/1000;
	mins=secs/60;
	secs=secs%60;

	if (playpos >= inpoint) printf("(%u:%02u after startpoint) \n",mins,secs);
	else                    printf("(%u:%02u before startpoint) \n",mins,secs);
}


/* This function plays sound up to the current position */
void playtoend(long playpos)
{
	long pos;
	int mins,secs;
	long framenr,rewframes;

	secs=abs(pos2time(inpoint)-pos2time(playpos))/1000;
	mins=secs/60;
	secs=secs%60;

	/* go rewframes frames back! */
	rewframes=(double)howlong*1000/(double)fix_frametime;
	framenr=framenumber-rewframes;
	if (framenr<0) framenr=0;

	pos=frame2pos(framenr);

	if (mute)
	{
		if (playpos >= inpoint)
		printf("  position is %4u:%05.2f / %u:%02.0f  (%u:%02i after startpoint) \n",frame2mins(framenumber),frame2secs(framenumber),totalmins,totalsecs,mins,secs);
		else
		printf("  position is %4u:%05.2f / %u:%02.0f  (%u:%02i before startpoint) \n",frame2mins(framenumber),frame2secs(framenumber),totalmins,totalsecs,mins,secs);
	}
	else
	{
		printf("\n  Now playing up to location.  ");
		playsel(pos);
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
void playfirst(long playpos)
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
	printf("\n   0:00.00 [1..0,r,a,b,s,q] >  ");
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
	long i;

	if (debug==7) printf("savesel(): ss=%.2f es=%.2f ip=%ld op=%ld \n",startsecs,endsecs,inpoint,outpoint);

	/* correct mins and secs */
	if (startsecs>59.999){ do {startmins++; startsecs=startsecs-60;} while (startsecs>59.999);}
	if (endsecs>59.999) { do {endmins++; endsecs=endsecs-60;} while (endsecs>59.999);}

	/* Import title from tags. Prefer V1 over V2!
	   No user interaction in this function */
	zaptitle();
	importid3v2(inpoint);
	importid3v1(outpoint);

	/* title known? */
	if (!usefilename && !no_tags && !forced_prefix && strlen(title)>0)
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
				fprintf(stdout,"  cutmp3: startpoint (%u:%05.2f) must be before end of file (%u:%05.2f)!  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(filesize),pos2secs(filesize));
				return;
			}
			else
			{
				printf("  ERROR: startpoint (%u:%05.2f) must be before end of file (%u:%05.2f)!  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(filesize),pos2secs(filesize));
				return;
			}
		}
		if (inpoint < audiobegin)
		{
			if (nonint==1)
			{
				fprintf(stdout,"  cutmp3: setting startpoint (%u:%05.2f) to beginning of file (%u:%05.2f)!  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(audiobegin),pos2secs(audiobegin));
				inpoint=audiobegin;
				return;
			}
			else
			{
				printf("  WARNING: setting startpoint (%u:%05.2f) to beginning of file (%u:%05.2f)!  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(audiobegin),pos2secs(audiobegin));
				inpoint=audiobegin;
			}
		}
		if (outpoint > filesize)
		{
			if (nonint==1)
			{
	// 			printf("%ld %ld",outpoint,filesize);
				fprintf(stdout,"  cutmp3: setting endpoint (%u:%05.2f) to end of file (%u:%05.2f)!  \n",pos2mins(outpoint),pos2secs(outpoint),pos2mins(filesize),pos2secs(filesize));
	// 			return;
			}
			else
			{
				printf("  WARNING: setting endpoint (%u:%05.2f) to end of file (%u:%05.2f)!  \n",pos2mins(outpoint),pos2secs(outpoint),pos2mins(filesize),pos2secs(filesize));
			}
			outpoint=filesize;
		}
		if (outpoint <= inpoint)
		{
			if (nonint==1)
			{
				fprintf(stdout,"  cutmp3: endpoint (%u:%05.2f) must be after startpoint (%u:%05.2f)!  \n",pos2mins(outpoint),pos2secs(outpoint),pos2mins(inpoint),pos2secs(inpoint));
				return;
			}
			else
			{
				printf("  ERROR: endpoint (%u:%05.2f) must be after startpoint (%u:%05.2f)!  \n",pos2mins(outpoint),pos2secs(outpoint),pos2mins(inpoint),pos2secs(inpoint));
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
	if (copytags==1){inpoint=rewind3v2(inpoint); outpoint=skipid3v1(outpoint);}

	/* do not copy id3 in case of -C switch */
	if (no_tags){inpoint=skipid3v2(inpoint); outpoint=rewind3v1(outpoint);}

	/* COPY DATA HERE */
	fseek(mp3file, inpoint, SEEK_SET);
	for (bytesin=0 ; bytesin < outpoint-inpoint ; bytesin++)
	{
		if (stdoutwrite==1) putchar(getc(mp3file));
		else fputc(getc(mp3file),outfile);
	}

	/* copy tag in case of -c switch */
	if (copytags==1 && importid3v1(filesize)>0)
	{
		for (i=0;i<128;i++)
		{
			if (stdoutwrite==1) putchar(id3v1[i]);
			else fputc(id3v1[i],outfile);
		}
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
	if (nonint==1 && negstart>0 && negend>0) printf("  saved %i:%05.2f - %i:%05.2f to '%s'.  \n",startmins,startsecs,endmins,endsecs,outname);
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
		printf("  saved %i:%05.2f - %i:%05.2f to '%s'.  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(outpoint),pos2secs(outpoint),outname);
	}
	else
	/* interactive cutting: */
	printf("  saved %u:%05.2f - %u:%05.2f to '%s'.  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(outpoint),pos2secs(outpoint),outname);

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
	char outname2[8191]="\0";
	char *newname=outname2;
	char *tmp;
	FILE *fp;
	FILE *outfile;
	long oldinpoint=inpoint;
	long oldoutpoint=outpoint;
	long bytesin;
	long length;

	temptitle[0]='\0';
	tempartist[0]='\0';
	tempalbum[0]='\0';
	tempyear[0]='\0';
	tempcomment[0]='\0';
	title1[0]='\0';
	title2[0]='\0';

	if (inpoint > filesize)
	{
		printf("  ERROR: startpoint (%u:%05.2f) must be before end of file (%u:%05.2f)!  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(filesize),pos2secs(filesize));
		return;
	}
	if (inpoint < audiobegin)
	{
		printf("  WARNING: setting startpoint (%u:%05.2f) to beginning of file (%u:%05.2f)!  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(audiobegin),pos2secs(audiobegin));
		inpoint=audiobegin;
	}
	if (outpoint > filesize)
	{
		printf("  WARNING: setting endpoint (%u:%05.2f) to end of file (%u:%05.2f)!  \n",pos2mins(outpoint),pos2secs(outpoint),pos2mins(filesize),pos2secs(filesize));
		outpoint=filesize;
	}
	if (outpoint <= inpoint)
	{
		printf("  ERROR: endpoint (%u:%05.2f) must be after startpoint (%u:%05.2f)!  \n",pos2mins(outpoint),pos2secs(outpoint),pos2mins(inpoint),pos2secs(inpoint));
		return;
	}

	/*********************************/
	/* Choose ID3 V2 or V1 or custom */
	/*********************************/
	hasid3=hastag=0;
// 	inpoint=rewind3v2(inpoint);  not always useful!

	zaptitle(); importid3v1(outpoint);
	if (strlen(title)>0) {hastag=1; snprintf(title1,31,title);} /* check if selection has tag v1 */

	zaptitle(); length=importid3v2(inpoint);
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
			importid3v1(outpoint);
		}
		else if (tagver=='2')
		{
			length=importid3v2(inpoint);
		}
	}
	else if (hasid3>hastag)   /* only V2 */
	{
		length=importid3v2(inpoint);
		printf("\n\nImported title from %s:",filename);
		printf("\nPress 2 for this title: %s",title);
		printf("\nOr press any other key for a custom tag  ");
		tagver=getchar();
		if (tagver!='2') tagver='3'; /* just in case someone pressed 1 */
	}
	else if (hastag>hasid3)  /* only V1 */
	{
		importid3v1(outpoint);
		printf("\n\nImported title from %s:",filename);
		printf("\nPress 1 for this title: %s",title);
		printf("\nOr press any other key for a custom tag  ");
		tagver=getchar();
		if (tagver!='1') tagver='3'; /* just in case someone pressed 2 */
	}

	else if (hastag+hasid3==0) /* no tag at all, get them from whole file */
	{
		hasid3=hastag=0;
		zaptitle(); importid3v1(filesize); if (strlen(title)>0) {hastag=1; snprintf(title1,31,title);}
		zaptitle(); length=importid3v2(0);        if (strlen(title)>0) {hasid3=1; snprintf(title2,31,title);}
		if (hastag+hasid3==2) /* whole file has both tags */
		{
 			printf("\n\nImported titles from %s:",filename);
			printf("\nPress 1 for this title: %s",title1);
			printf("\nPress 2 for this title: %s",title2);
			printf("\nOr press any other key for a custom tag  ");
			tagver=getchar();
			if (tagver=='1') importid3v1(filesize);
			else if (tagver=='2') length=importid3v2(0);
		}
		else if (hasid3>hastag)         /* has V2 info from whole file */
		{
			length=importid3v2(0);
 			printf("\n\nImported title from %s:",filename);
			printf("\nPress 2 for this title: %s",title);
			printf("\nOr press any other key for a custom tag  ");
			tagver=getchar();
			if (tagver!='2') tagver='3'; /* just in case someone pressed 1 */
		}
		else if (hastag>hasid3)        /* has V1 info from whole file  */
		{
			importid3v1(filesize);
 			printf("\n\nImported title from %s:",filename);
			printf("\nPress 1 for this title: %s",title);
			printf("\nOr press any other key for a custom tag  ");
			tagver=getchar();
			if (tagver!='1') tagver='3'; /* just in case someone pressed 2 */
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
		inpoint=skipid3v2(inpoint); /* remove possible id3 at beginning */
		fseek(mp3file, inpoint, SEEK_SET);
		/* copy audio data */
		for (bytesin=0; bytesin < outpoint-inpoint; bytesin++) fputc(getc(mp3file),outfile);
		/* copy id3 v1 tag from end of file if not from selection */
		if (importid3v1(filesize)>0 && importid3v1(outpoint)==0)
		{
			for (i=0;i<128;i++) fputc(id3v1[i],outfile);
		}
		printf(" finished.\n");
	}
	else if (tagver=='2') /* if chosen to copy ID3 V2 */
	{
		/* copy id3 v2 tag */
		if (debug==5){printf(" ID3V2 length=%ld ",length);}
		if (length>0) for (i=0;i<length;i++) fputc(id3v2[i],outfile);

		outpoint=rewind3v1(outpoint); /* remove possible V1 tag at end */
		inpoint=skipid3v2(inpoint); /* remove possible id3 at beginning */

		/* copy audio data */
		for (bytesin=0; bytesin < outpoint-inpoint; bytesin++) fputc(getc(mp3file),outfile);
		printf(" finished.\n");
	}
	else /* custom tag */
	{
		inpoint=skipid3v2(inpoint); /* remove possible id3 at beginning */
		outpoint=rewind3v1(outpoint); /* remove possible tag at end */

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

	if (tagver=='2') printf("\n  saved %u:%02.0f - %u:%02.0f with ID3 V2 tag to '%s'.  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(outpoint),pos2secs(outpoint),newname);
	else printf("\n  saved %u:%02.0f - %u:%02.0f with ID3 V1 tag to '%s'.  \n",pos2mins(inpoint),pos2secs(inpoint),pos2mins(outpoint),pos2secs(outpoint),newname);

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


void cutexact(char *prefix)
{
	double seektime;
	long framenr;

	if (debug==7) printf("cutexact()\n");

	if (debug==7) printf(" %i %i %lf %i %i %lf \n",negstart,startmins,startsecs,negend,endmins,endsecs);

	/* first look for inpoint */

	if (debug==7) printf(" sm=%u ss=%lf \n",startmins,startsecs);
	if (negstart<0)
	{
		seektime=endtime-startmins*60000-startsecs*1000;
		framenr=seektime/fix_frametime;
		if (debug==7) printf(" st=%lf fn=%ld\n",seektime,framenr);
		if (framenr<=0) inpoint=0;
		else inpoint=frame2pos(framenr);
		if (debug==7) printf(" ss=%.2f es=%.2f ip=%ld op=%ld fn=%ld\n",startsecs,endsecs,inpoint,outpoint,framenr);
	}
	else
	{
		seektime=startmins*60000+startsecs*1000;
		framenr=seektime/fix_frametime;
		if (debug==7) printf(" st=%lf fn=%ld\n",seektime,framenr);
		if (framenr>=totalframes) inpoint=filesize;
		else inpoint=frame2pos(framenr);
		if (debug==7) printf(" ss=%.2f es=%.2f ip=%ld op=%ld fn=%ld\n",startsecs,endsecs,inpoint,outpoint,framenr);
	}
// 	printf("  inpoint=%ld",inpoint);

	/* inpoint reached, now search for outpoint */

//	printf("  seeking endpoint");
	if (negend<0)
	{
		seektime=endtime-endmins*60000-endsecs*1000;
		framenr=seektime/fix_frametime;
		if (debug==7) printf(" st=%lf fn=%ld\n",seektime,framenr);
		if (framenr<=0) outpoint=0;
		else outpoint=frame2pos(framenr);
		if (debug==7) printf(" ss=%.2f es=%.2f ip=%ld op=%ld fn=%ld\n",startsecs,endsecs,inpoint,outpoint,framenr);
	}
	else
	{
		seektime=startmins*60000+startsecs*1000;
		framenr=seektime/fix_frametime;
		if (debug==7) printf(" st=%lf fn=%ld\n",seektime,framenr);
		if (framenr>=totalframes) outpoint=filesize;
		else outpoint=frame2pos(framenr);
		if (debug==7) printf(" ss=%.2f es=%.2f ip=%ld op=%ld fn=%ld\n",startsecs,endsecs,inpoint,outpoint,framenr);
	}

	/* outpoint reached, now write file */

//	printf("  saving selection...\n");
	if (debug==7) printf(" ss=%.2f es=%.2f ip=%ld op=%ld fn=%ld\n",startsecs,endsecs,inpoint,outpoint,framenr);
	savesel(prefix);
	if (debug==7) printf(" ss=%.2f es=%.2f ip=%ld op=%ld fn=%ld\n",startsecs,endsecs,inpoint,outpoint,framenr);
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

	startsecs=endsecs=0; /* seconds _may_ be given */
	startmins=endmins=0; /* minutes _must_ be given! */
	negstart=negend=1;

	if (debug==7) printf(" cutfromtable()\n");

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

		if (debug==7) printf(" %i %i %lf %i %i %lf \n",negstart,startmins,startsecs,negend,endmins,endsecs);

		/* space switches to read end or to cutexact() */
		if (isspace(ttable[pos2]))  /* char is a whitespace */
		{
		if (debug==7) printf("whitespace %i %i %lf %i %i %lf \n",negstart,startmins,startsecs,negend,endmins,endsecs);
			if (position==5 && endmins+endsecs > 0)  /* only minutes given, finish */
			{
				position=9;
			}
			if (position < 5) position=5;  /* read end now */
			if ((position > 4 && endmins+endsecs > 0) || (position > 4 && endmins+endsecs==0 && negend==-1) ) /* cut exact now */
			{
				/* correct mins and secs */
				if (startsecs>59.999){ do {startmins++; startsecs=startsecs-60;} while (startsecs>59.999);}
				if (endsecs>59.999) { do {endmins++; endsecs=endsecs-60;} while (endsecs>59.999);}

				if (a_b_used!=1) printf(" using timetable \"%s\" line %i:\n",tablename,linenumber);linenumber++;
				if (endmins*negend>totalmins)
				{
					printf("  WARNING: setting endpoint (%i:%05.2f) to end of file (%u:%05.2f)!  \n",endmins,endsecs,pos2mins(filesize),pos2secs(filesize));
					endmins=totalmins;
					endsecs=totalsecs;
				}
				if (endmins*negend==totalmins && endsecs>totalsecs)
				{
// 					printf("%ld %ld",outpoint,filesize);
					printf("  WARNING: setting endpoint (%i:%05.2f)  to end of file (%u:%05.2f)!  \n",endmins,endsecs,pos2mins(filesize),pos2secs(filesize));
					endsecs=totalsecs;
				}
				if (startmins*negstart>totalmins)
				{
					printf("  ERROR: startpoint (%i:%05.2f) is after end of file (%u:%05.2f)!  \n",startmins,startsecs,pos2mins(filesize),pos2secs(filesize));
				}
				if (startmins*negstart==totalmins && startsecs>totalsecs)
				{
					printf("  ERROR: startpoint (%i:%05.2f)  is after end of file (%u:%05.2f)!  \n",startmins,startsecs,pos2mins(filesize),pos2secs(filesize));
				}
				/* end after start? -> check in cutexact() */
				else cutexact(prefix);
				position=1;
				startsecs=endsecs=0; /* seconds _may_ be given */
				startmins=endmins=0; /* minutes _must_ be given! */
				negstart=negend=1;
				number=0;
			}
		}

		if (ttable[pos2]==255) pos2=BUFFER;
		if (debug==7) printf("pos2=%u %i:%02.2f %i:%02.2f position=%u\n",pos2,startmins,startsecs,endmins,endsecs,position);
		pos2++;
	}
	while (pos2<BUFFER);
}


void showfileinfo(int rawmode, int fix_channels)
{
	unsigned char a,b,c;

	if (rawmode==1)
	{
		printf("%ld %u %u %.0f %u %.0lf\n",totalframes,fix_sampfreq,fix_channels,avbr,fix_mpeg,fix_frametime*totalframes);
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
	printf("*\n* Filelength is %u minute(s) and %4.2f second(s)\n",totalmins,totalsecs);

	/* show tag V1 if any: */
	fseek(mp3file, filesize-128, SEEK_SET); /* set to right position */
	a=fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);
	if (a=='T' && b=='A' && c=='G' ) print_id3v1(filesize); /* TAG is there */
	else printf("\nno ID3 V1 tag.\n");

	/* show tag V2 if any: */
	zaptitle(); importid3v2(0);
	if (strlen(title)>0)
	{
		printf("\nID3 V2.%u tag:\nTitle:   %s",minorv,title);
		printf("\nArtist:  %s",artist);
		printf("\nAlbum:   %s",album);
		printf("\nYear:    %s",year);
		printf("\nTrack:   %s",track);
		printf("\nComment: %s\n",comment);
	}
	else printf("\nno ID3 V2 tag.\n");
	return;

}


void showdebug(long seekpos, char *prefix)
{
	int a,b,c,d;

	printf("\n\nfile name is %s\n",filename);
	printf("audiobegin is at      %10i\n",audiobegin);
	printf("I am at offset        %10li <-\n",seekpos);
// 	printf("frame2pos             %10li\n",frame2pos(framenumber));
	printf("last frame, last byte %10li\n",dataend);
	printf("file size is          %10li\n",filesize);
	printf("inpoint is            %10li\n",inpoint);
	printf("outpoint is           %10li\n",outpoint);
	fseek(mp3file, seekpos, SEEK_SET);
	a=fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);
	d=fgetc(mp3file);
	printf("current framesize is %i bytes\n",framesize(b,c,d));
	printf("pos2time is %lf\n",pos2time(seekpos));
	printf("current framenumber is %10li\n",framenumber);
	printf("pos2frame is           %10li\n",pos2frame(seekpos));
	printf("total # of frames is   %10li\n",totalframes);
// 	printf("framenumber(filesize) is %ld\n",pos2frame(filesize));
// 	printf("framenumber(dataend) is %ld\n",pos2frame(dataend));
	printf("fix framesize is %i bytes\n",fix_frsize);
	printf("fix frametime is %.5lf ms\n",fix_frametime);
// 	printf("framepos1000[0] is %ld\n",framepos1000[0]);
// 	printf("framepos1000[1] is %ld\n",framepos1000[1]);
	printf("silencelength is %i ms\n",silencelength);
	printf("silvol is %i\n",silvol);
	printf("volume is %i\n",volume(seekpos));
	printf("msec is %lf bytes\n",msec);
	printf("Prefix is '%s'\n",prefix);
	print_id3v1(seekpos);
	print_id3v2(seekpos);
}


/* ------------------------------------------------------------------------- */
/*      */
/* MAIN */
/*      */
/* ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	int a,b,c,d,char1,showinfo=0,rawmode=0,fix_channels=0;
	long pos, startpos;
	double ft_factor=2;
	char *tablename=malloc(8191);
	char *prefix=malloc(8191);
	char pprefix[8191];
	void (*play)(long playpos) = playsel;

/* ------------------------------------------------------------------------- */
/*                   */
/* parse commandline */
/*                   */
/* ------------------------------------------------------------------------- */

	if ((fdlog = mkstemp(logname)) == -1 || (NULL== (logfile = fdopen(fdlog, "w+b"))) )
	{
		perror("\ncutmp3: failed writing temporary logfile in /tmp");exitseq(11);
	}

	readconf();       /* read configuration file first! */

	if (argc<2) {usage("ERROR: missing filename");exitseq(1);}
	else
	{
		while ((char1 = getopt(argc, argv, "cCkheqFf:a:b:i:o:O:I:d:D:s:")) != -1)
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
// 					exactmode=1;
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
						if (optarg[0]=='-') stdoutwrite=1;
					}
					break;
				case 's':
					if (optarg!=0) silfactor=atoi(optarg); /* changes maximum silence length */
					break;
				case 'k':
					usefilename=1;
					break;
				default:
					exitseq(1);
					break;
			}
		}
	}

	if (strlen(prefix)==0) prefix="result";
	if (usefilename) {strncpy(pprefix,filename,strlen(filename)-4); prefix=pprefix;}

	if (userin==0 && userout!=0) usage("ERROR: missing inpoint");
	if (userin!=0 && userout==0) usage("ERROR: missing outpoint");
	if (filename==0) {usage("ERROR: missing filename, use 'cutmp3 -i file.mp3 [options]'");exitseq(1);}

	if (stdoutwrite==0) setvbuf(stdout,NULL,_IONBF,0); /* switch off buffered output to stdout when writing to file */

	mp3file = fopen(filename, "rb");

	/* get filesize and set outpoint to it */
	fseek(mp3file, 0, SEEK_END);
	outpoint=filesize=ftell(mp3file);

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

/********** global values: **********/

	fix_secondbyte=begin[1];
	fix_sampfreq=sampfreq(begin[1],begin[2]);
	if (fix_sampfreq==1) usage("ERROR: Could not determine sampling frequency.");
	fix_channelmode=channelmode(begin[3]);
	fix_channels=channels(begin[3]);
	fix_mpeg=mpeg(begin[1]);
	if (fix_mpeg==1) ft_factor=1;
	else ft_factor=2;

	fix_frametime=(double)1152000/(double)fix_sampfreq/(double)ft_factor;  /* in msecs, half the time if MPEG>1 */

	avbr=avbitrate(); /* average bitrate during SCANAREA (10 MB) */
	if (avbr==0) {usage("ERROR: File has less than two valid frames.");exitseq(4);}
// 	msec=avbr/8;  /* one millisecond in bytes */
	if (!vbr) fix_frsize=framesize(begin[1],begin[2],begin[3]);
	scanframes();
	msec=frame2time(totalframes)*1000/filesize;  /* one millisecond in bytes */

	dataend=prevframe(filesize); /* Beginning of last frame */
	fseek(mp3file, dataend, SEEK_SET);
	fgetc(mp3file);
	b=fgetc(mp3file);
	c=fgetc(mp3file);
	d=fgetc(mp3file);
	dataend=dataend+framesize(b,c,d)-1; /* last byte of last frame */
//	printf("dataend=%ld  filesize=%ld  nextframe(dataend)=%ld",dataend,filesize,nextframe(dataend));

	/* get total mins:secs */
	endtime=totalframes*fix_frametime;
	totalmins=endtime/60000;
	totalsecs=(endtime-(double)totalmins*(double)60000)/1000;

/********** End of global part, now do specific parts **********/

	/* Show file info only? */
	if (showinfo==1)
	{
		showfileinfo(rawmode,fix_channels);
		printf("\n");
		exitseq(0);
	}

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
 		printf("\n%4u:%05.2f [1..0,r,a,b,s,q] > ", pos2mins(startpos), pos2secs(startpos));
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
		if (c == 'N') {howlong=howlong-1;if (howlong<1)howlong=1;printf("  playing %i second(s) now\n",howlong);}
		if (c == 'M') {howlong=howlong+1;printf("  playing %i second(s) now\n",howlong);}
		if (c == 'z') showdebug(startpos,prefix);
		if (c == 'a')
		{
			inpoint=startpos;
			intime=frame2time(framenumber);
			printf("  startpoint set to %u:%05.2f  ",pos2mins(inpoint),pos2secs(inpoint));
// 			play=playtoend;
		}
		if (c == 'b')
		{
			outpoint=startpos;
			outtime=frame2time(framenumber);
			printf("  endpoint set to %u:%05.2f  ",pos2mins(outpoint),pos2secs(outpoint));
// 			play=playsel;
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
				printf("\n  End of silence reached.       \n");
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
				printf("\n  Beginning of silence reached.\n");
// 				play=playtoend;
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
			outpoint=startpos;printf("  endpoint set to %u:%05.2f  ",pos2mins(outpoint),pos2secs(outpoint));
			savewithtag();
			inpoint=startpos;printf("  startpoint set to %u:%05.2f  \n",pos2mins(inpoint),pos2secs(inpoint));
// 			play=playtoend;
		}
		if (c == 'w')
		{
			outpoint=startpos;printf("  endpoint set to %u:%05.2f  \n",pos2mins(outpoint),pos2secs(outpoint));
			savesel(prefix);
			inpoint=startpos;printf("  startpoint set to %u:%05.2f  \n",pos2mins(inpoint),pos2secs(inpoint));
// 			play=playtoend;
		}
		if (c == 'A')
		{
			startpos=inpoint;
			framenumber=pos2frame(inpoint);
			play(startpos);
		}
		if (c == 'B')
		{
			startpos=outpoint;
			framenumber=pos2frame(outpoint);
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
			silencelength=silencelength*1.101;
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
0 - 7   Alles 1 für Sync
########
8 -10   Alles 1 für Sync
11-12   MPEG-Version: 10=MPEG2 11=MPEG1 01=MPEG2.5
13-14   0 1 für Layer3
        1 0 für Layer2
        1 1 für Layer1
   15   Protection Bit, bei 0 soll nach Header Error-Check kommen (CRC)
########
16-19   bitrate encoding
20-21   Sampling Frequency
   22   Padding Bit, if set, frame is 1 byte bigger
   23   Private Bit
########
24-25   Mono/Stereo Modus
26-27   Mode Extension für Joint Stereo
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
