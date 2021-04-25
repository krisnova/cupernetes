/*
 *  Canon Inkjet Printer Driver for Linux
 *  Copyright CANON INC. 2001-2017
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 *
 * NOTE:
 *  - As a special exception, this program is permissible to link with the
 *    libraries released as the binary modules.
 *  - If you write modifications of your own for these programs, it is your
 *    choice whether to permit this exception to apply to your modifications.
 *    If you do not wish that, delete this exception.
*/

//#include <cups/cups.h>
//#include <cups/ppd.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <dlfcn.h>
#include <time.h>

//#include "./cnclinc/cncl.h"
#include "./cnclinc/cncldef.h"
#include "./cnclinc/cnclcmdutils.h"

#include "cnijutil.h"

#if HAVE_CONFIG_H
#include <config.h>
#endif

#define SAFE_FREE(p)	{if((p)!=NULL)free(p);(p)=NULL;}

#define CN_LIB_PATH_LEN		(512)
#define CN_CNCL_LIB_PATH	("libcnbpcnclapi%s.so")

#define	DATA_BUF_SIZE	(1024)
#define	PARAM_BUF_SIZE	(256)

#define IS_RETURN(c)	(c == '\r' || c == '\n')
#define COUNTOF(list, type)		(sizeof(list)/sizeof(type))

#define PROC_FAILED		(1)
#define PROC_SUCCEEDED	(0)

#define CN_START_JOBID		("00000001")
#define CN_START_JOBID2		("00000002")
#define CN_START_JOBID_LEN	(9)

#define CMD_TEST_PRINT	(0)
#define CMD_CLEANING	(1)

#define UTIL_CMD_TEST_PRINT     ("PrintSelfTestPage")
#define UTIL_CMD_CLEANING       ("Clean all")

#define CN_BUFSIZE				(1024 * 4)
#define CN_CAPA_SIZE_MNT		(1024 * 32)

#define ERR_MAKE_COMMAND	(-1)
#define ERR_WRITE_COMMAND	(-2)
#define ERR_NOT_SUPPORT		(-3)
#define ERR_FATAL			(-65535)


int g_signal_received = 0;

static	void	*_libclss = NULL;

static	int		(*GetStringWithTagFromFile)( const char* , const char* , int* , uint8_t** );
static	int		(*MakeCommand_mnt_TestPrint)(char[], void *, int, long *);
static	int		(*MakeCommand_mnt_Cleaning)(char[], void *, int, long *);
static	int		(*MakeCommand_mnt_StartJob3)(int, char *, char[], void *, long, long *);
static	int		(*MakeCommand_mnt_EndJob)(char[], void *, long, long *);
static	int		(*ParseCapabilityResponseMaintenance_HostEnv)(void *, int);
static	int		(*ParseCapabilityResponseMaintenance_DateTime)(void *, int);
static 	int		(*MakeCommand_mnt_SetJobConfiguration)(char[], char[], void *, int, long *);



//////////////////////////////////////////////////////////////////////////////


static int read_line(int fd, char *p_buf, int bytes){
	static char read_buf[DATA_BUF_SIZE];
	static int buf_bytes = 0;
	static int buf_pos = 0;
	int read_bytes = 0;

	memset(p_buf, 0, bytes);
	while( bytes > 0 )
	{
		if( buf_bytes == 0 && fd != -1 )
		{
			buf_bytes = read(fd, read_buf, DATA_BUF_SIZE);
			if( buf_bytes > 0 ) buf_pos = 0;
		}

		if( buf_bytes > 0 )
		{
			*p_buf = read_buf[buf_pos++];
			bytes--;
			buf_bytes--;
			read_bytes++;

			if( IS_RETURN(*p_buf) ) break;
			p_buf++;
		}
		else if( buf_bytes < 0 )
		{
			if( errno == EINTR ) continue;
		}
		else
			break;
	}

	return read_bytes;
}


static int write_buffer(const uint8_t *buffer, const size_t size){
	size_t writtenSize = 0;

	for( writtenSize = 0; writtenSize < size; ){
		size_t w = write( 1, &buffer[writtenSize], size - writtenSize );

		if ( w < 0 ){
			return PROC_FAILED;
		}

		writtenSize += w;
	}

	fsync(1);

	return PROC_SUCCEEDED;
}


static void sigterm_handler(int sigcode){
	g_signal_received = 1;
}


int MakeCommand_StartJob3( char *capXml, int capXmlSize, uint8_t *cmdBuf, long cmdSize, long *writtenSize, char *jobDesc ){
	int rtn = PROC_FAILED;
	// long writtenSize = 0;

	int hostEnv = 0;


	ParseCapabilityResponseMaintenance_HostEnv = dlsym( _libclss, "CNCL_ParseCapabilityResponseMaintenance_HostEnv" );
	if ( dlerror() != NULL ) {
		fprintf( stderr, "Load Error in CNCL_ParseCapabilityResponseMaintenance_HostEnv\n" );
		goto error_exit;
	}

	MakeCommand_mnt_StartJob3 = dlsym( _libclss, "CNCL_MakeCommand_StartJob3_Maintenance" );

	if ( dlerror() != NULL ) {
		fprintf( stderr, "Load Error in CNCL_MakeCommand_StartJob3\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}

	// check hostEnvironment
	hostEnv = ParseCapabilityResponseMaintenance_HostEnv( capXml, capXmlSize );

	// make command (start job)
	rtn = MakeCommand_mnt_StartJob3( hostEnv, jobDesc, CN_START_JOBID2, cmdBuf, cmdSize, writtenSize );

	if( rtn != PROC_SUCCEEDED ){
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}

	rtn = PROC_SUCCEEDED;

error_exit:
	return rtn;
}

int MakeCommand_SetJobConfiguration( char *capXml, int capXmlSize, uint8_t *cmdBuf, long cmdSize, long *writtenSize ){
	int rtn = PROC_FAILED;
	// long writtenSize = 0;


	ParseCapabilityResponseMaintenance_DateTime = dlsym( _libclss, "CNCL_ParseCapabilityResponseMaintenance_DateTime" );

	if ( dlerror() != NULL ) {
		fprintf( stderr, "Load Error in CNCL_ParseCapabilityResponseMaintenance_DateTime\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}


	MakeCommand_mnt_SetJobConfiguration = dlsym( _libclss, "CLSS_MakeCommand_SetJobConfiguration_Maintenance" );

	if ( dlerror() != NULL ) {
		fprintf( stderr, "Load Error in CLSS_MakeCommand_SetJobConfiguration_Maintenance\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}


	rtn = ParseCapabilityResponseMaintenance_DateTime( capXml, capXmlSize );

	if( rtn == 2 ){
		char dateTime[15];

		memset(dateTime, '\0', sizeof(dateTime));

		time_t timer = time(NULL);
		struct tm *date = localtime(&timer);

		sprintf(dateTime, "%d%02d%02d%02d%02d%02d",
			date->tm_year+1900, date->tm_mon+1, date->tm_mday,
			date->tm_hour, date->tm_min, date->tm_sec);

		// memset( command_buffer, 0x00, CN_BUFSIZE );

		rtn = MakeCommand_mnt_SetJobConfiguration( CN_START_JOBID2, dateTime, cmdBuf, cmdSize, writtenSize );

		if( rtn != PROC_SUCCEEDED ){
			rtn = ERR_MAKE_COMMAND;
			goto error_exit;
		}
	}
	else{
		rtn = ERR_NOT_SUPPORT;
	}

	rtn = PROC_SUCCEEDED;


error_exit:
	return rtn;
}

int MakeCommand_EndJob( uint8_t *cmdBuf, long cmdSize, long *writtenSize ){
	int rtn = PROC_FAILED;
	// long writtenSize = 0;

	MakeCommand_mnt_EndJob = dlsym( _libclss, "CNCL_MakeCommand_EndJob_Maintenance" );

	if ( dlerror() != NULL ) {
		fprintf( stderr, "Load Error in CNCL_MakeCommand_EndJob_Maintenance\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}


	rtn = MakeCommand_mnt_EndJob( CN_START_JOBID2, cmdBuf, cmdSize, writtenSize );

	if ( rtn != PROC_SUCCEEDED ){
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}

	rtn = PROC_SUCCEEDED;


error_exit:
	return rtn;
}

int MakeCommand_TestPrint( uint8_t *cmdBuf, long cmdSize, long *writtenSize ){
	int rtn = PROC_FAILED;
	// long writtenSize = 0;


	MakeCommand_mnt_TestPrint = dlsym( _libclss, "CNCL_MakeCommand_TestPrint" );

	if ( dlerror() != NULL ) {
		fprintf( stderr, "[cmdtocanonij3] Load Error in CNCL_MakeCommand_TestPrint\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}

	rtn = MakeCommand_mnt_TestPrint( CN_START_JOBID2, cmdBuf, cmdSize, writtenSize );

	if ( rtn != PROC_SUCCEEDED ) {
		fprintf( stderr, "[cmdtocanonij3] Error in CNCL_MakeCommand_TestPrint\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}

	rtn = PROC_SUCCEEDED;


error_exit:
	return rtn;
}

int MakeCommand_Cleaning( uint8_t *cmdBuf, int cmdSize, long *writtenSize ){
	int rtn = PROC_FAILED;
	// int writtenSize = 0;


	MakeCommand_mnt_Cleaning = dlsym( _libclss, "CNCL_MakeCommand_Cleaning" );

	if ( dlerror() != NULL ) {
		fprintf( stderr, "[cmdtocanonij3] Load Error in CNCL_MakeCommand_Cleaning\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}

	rtn = MakeCommand_mnt_Cleaning( CN_START_JOBID2, cmdBuf, cmdSize, writtenSize );

	if ( rtn != PROC_SUCCEEDED ) {
		fprintf( stderr, "[cmdtocanonij3] Error in CNCL_MakeCommand_Cleaning\n" );
		rtn = ERR_MAKE_COMMAND;
		goto error_exit;
	}

	rtn = PROC_SUCCEEDED;


error_exit:
	return rtn;
}


int getMaintenanceType( int ifd, int *type ){
	char read_buf[DATA_BUF_SIZE];
	int read_bytes;
	int rtn = PROC_FAILED;
	int lines = 0;

	memset(read_buf, 0, sizeof(read_buf));

	for( lines = 0; ( read_bytes = read_line( ifd, read_buf, DATA_BUF_SIZE - 1 ) ) > 0; lines++ ){

		// check cups command file suffix.
		if (lines == 0){
			if (strncmp(read_buf, "#CUPS-COMMAND", read_bytes-1) != 0){
				fprintf(stderr, "DEBUG: [cmdtocanonij3] command file data are invalid.\n");
				goto error_exit;
			}
			else
				continue;
		}

		// get device command
		// char mnt_command[PARAM_BUF_SIZE];

		// if (get_printer_command(&read_buf[0], read_bytes, &mnt_command[0], PARAM_BUF_SIZE) != PROC_SUCCEEDED){
		// 	fprintf(stderr, "DEBUG: [cmdtocanonij3] get_printer_command failed.\n");
		// 	goto error_exit;
		// }

		// perform only first command.
		// if( strncmp( mnt_command, UTIL_CMD_TEST_PRINT, strlen( mnt_command ) ) == 0 ){


		if( strncmp( read_buf, UTIL_CMD_TEST_PRINT, strlen( read_buf ) - 1 ) == 0 ){
			*type = CMD_TEST_PRINT;
			rtn = PROC_SUCCEEDED;
			break;
		}
		// else if( strncmp( mnt_command, UTIL_CMD_CLEANING, strlen( mnt_command ) ) == 0 ){
		else if( strncmp( read_buf, UTIL_CMD_CLEANING, strlen( read_buf ) - 1 ) == 0 ){
			*type = CMD_CLEANING;
			rtn = PROC_SUCCEEDED;
			break;
		}
		else{
			fprintf(stderr, "DEBUG: [cmdtocanonij3] cannot detect command.\n");
			break;
		}
	}

	rtn = PROC_SUCCEEDED;


error_exit:
	return rtn;
}



int main( int argc, char *argv[] ){

	int		rtn = PROC_FAILED;

	int		ifd = 0;
	struct	sigaction sigact;

	uint8_t	*cmdBuf;
	long	writtenSize = 0;
	int		dType = -1;

	char	model_number[] = "com2";
	char	libPathBuf[CN_LIB_PATH_LEN];

	const char *p_ppd_name = getenv("PPD");
	char	*xmlBuf_maintenance = NULL;
	int		xmlBufSize_maintenance = 0;

	// char	jobID[CN_START_JOBID_LEN];
	char	jobDesc[UUID_LEN + 1];


	cmdBuf = (uint8_t *) malloc( CN_BUFSIZE );

	if( cmdBuf == NULL ){
		rtn = PROC_FAILED;
		goto error_exit;
	}

	// xmlBuf_maintenance = (char *) malloc( CN_CAPA_SIZE_MNT );

	// if( xmlBuf_maintenance == NULL ){
	// 	rtn = PROC_FAILED;
	// 	goto error_exit;
	// }

	memset( cmdBuf, '\0', CN_BUFSIZE );
	// memset( xmlBuf_maintenance, '\0', CN_CAPA_SIZE_MNT );
	// memset( jobID, '\0', CN_START_JOBID_LEN );

	GetStringWithTagFromFile = NULL;
	ParseCapabilityResponseMaintenance_HostEnv = NULL;
	ParseCapabilityResponseMaintenance_DateTime = NULL;
	MakeCommand_mnt_StartJob3 = NULL;
    MakeCommand_mnt_EndJob = NULL;
	MakeCommand_mnt_TestPrint = NULL;
	MakeCommand_mnt_SetJobConfiguration = NULL;


	/* Set JobID */
	// strncpy( jobID, CN_START_JOBID2, sizeof(CN_START_JOBID_LEN) );

	setbuf(stderr, NULL);
	fprintf(stderr, "DEBUG: [cmdtocanonij3] started.\n");

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sigterm_handler;

	if( sigaction(SIGTERM, &sigact, NULL) ){
		fputs("ERROR: [cmdtocanonij3] can not register signal hander.\n", stderr);
		fprintf(stderr, "DEBUG: [cmdtocanonij3] can not register signal hander.\n");
		rtn = PROC_FAILED;
		goto error_exit;
	}

	if( argc < 6 || argc > 7 ){
		fputs("ERROR: [cmdtocanonij3] illegal parameter number.\n", stderr);
		fprintf(stderr, "DEBUG: [cmdtocanonij3] illegal parameter number.\n");
		rtn = PROC_FAILED;
		goto error_exit;
	}

	if( argc == 7 ){
		if( (ifd = open(argv[6], O_RDONLY)) == -1 ){
			fputs("ERROR: [cmdtocanonij3] cannot open file.\n", stderr);
			fprintf(stderr, "DEBUG: [cmdtocanonij3] cannot open file.\n");
			rtn = PROC_FAILED;
			goto error_exit;
		}
	}


// <<<<<<< HEAD
	// jobDesc = (char *) malloc( UUID_LEN + 1 );
	// memset( jobDesc, '\0', UUID_LEN + 1 );
// =======
	// memset( jobDesc, '\0', strlen(jobDesc) );
	memset( jobDesc, '\0', sizeof(jobDesc) );
// >>>>>>> parent of 1f4dac4... Test : cmdtocanonij3

	if( GetUUID( argv[5], jobDesc ) != 0 ){
		strncpy( jobDesc, argv[1], UUID_LEN );
	}

	snprintf( libPathBuf, CN_LIB_PATH_LEN - 1, CN_CNCL_LIB_PATH, model_number );

	_libclss = dlopen( libPathBuf, RTLD_LAZY );

	if ( !_libclss ) {
		fprintf( stderr, "Error in dlopen\n" );
		goto error_exit;
	}


	GetStringWithTagFromFile = dlsym( _libclss, "CNCL_GetStringWithTagFromFile" );

	if ( dlerror() != NULL ) {
		fprintf( stderr, "Error in CNCL_GetStringWithTagFromFile\n" );
		goto error_exit;
	}


	xmlBufSize_maintenance = GetStringWithTagFromFile( p_ppd_name, CNCL_FILE_TAG_CAPABILITY_MAINTENANCE, (int *)CNCL_DECODE_EXEC, (unsigned char **)&xmlBuf_maintenance );


	// get maintenance command type
	rtn = getMaintenanceType( ifd, &dType );


	if( rtn == PROC_SUCCEEDED ){

		// make command (StartJob)
		rtn = MakeCommand_StartJob3( xmlBuf_maintenance, xmlBufSize_maintenance, cmdBuf, CN_BUFSIZE, &writtenSize, jobDesc );

		if( rtn != PROC_SUCCEEDED ){
			goto error_exit;
		}

		// write command
		if ( write_buffer( cmdBuf, writtenSize ) != PROC_SUCCEEDED ){
			rtn = ERR_WRITE_COMMAND;
			goto error_exit;
		}

		memset( cmdBuf, '\0', CN_BUFSIZE );

		// make command (SetJobConfiguration)
		rtn = MakeCommand_SetJobConfiguration( xmlBuf_maintenance, xmlBufSize_maintenance, cmdBuf, CN_BUFSIZE, &writtenSize );

		if( rtn == PROC_SUCCEEDED ){
			// write command
			if ( write_buffer( cmdBuf, writtenSize ) != PROC_SUCCEEDED ){
				rtn = ERR_WRITE_COMMAND;
				goto error_exit;
			}
		}

		memset( cmdBuf, '\0', CN_BUFSIZE );


		if( dType == CMD_TEST_PRINT ){
			// make command (TestPrint)
			rtn = MakeCommand_TestPrint( cmdBuf, CN_BUFSIZE, &writtenSize );

			if( rtn != PROC_SUCCEEDED ){
				goto error_exit;
			}
		}
		else if( dType == CMD_CLEANING ){
			// make command (Cleaning)
			rtn = MakeCommand_Cleaning( cmdBuf, CN_BUFSIZE, &writtenSize );

			if( rtn != PROC_SUCCEEDED ){
				goto error_exit;
			}
		}

		// write command
		if ( write_buffer( cmdBuf, writtenSize ) != PROC_SUCCEEDED ){
			rtn = ERR_WRITE_COMMAND;
			goto error_exit;
		}

		memset( cmdBuf, '\0', CN_BUFSIZE );

		// make command (EndJob)
		rtn = MakeCommand_EndJob( cmdBuf, CN_BUFSIZE, &writtenSize );

		if( rtn != PROC_SUCCEEDED ){
			goto error_exit;
		}

		// write command
		if ( write_buffer( cmdBuf, writtenSize ) != PROC_SUCCEEDED ){
			rtn = ERR_WRITE_COMMAND;
			goto error_exit;
		}
	}
	else{
		goto error_exit;
	}

	rtn = PROC_SUCCEEDED;


end_proc:
	if( xmlBuf_maintenance != NULL ){
		free( xmlBuf_maintenance );
	}

	if ( _libclss != NULL ) {
		dlclose( _libclss );
	}

	if( cmdBuf != NULL ){
		free( cmdBuf );
	}

	return rtn;


error_exit:
	fprintf(stderr, "DEBUG: [cmdtocanonij3] exited with code %d.\n", rtn);
	goto end_proc;
}

