/* Glue to play a stream from a remote TiVo
 *
 * tivo@wingert.org, February 2003
 *
 * Copyright (C) 2003 Christopher R. Wingert
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mfs.h"
#include "vstream-client.h"

#define CHUNKSIZE                ( 128 * 1024 )

struct sNowShowing 
{
   char dirName[ 80 ];
   char blankTitle[ 80 ];
};

struct sNowShowing dirList[] =
{
   { "/Recording/LiveCache", "Live TV Buffer" },
   { "/Recording/NowShowingByTitle", "Now Showing Title" },
   { "/Recording/NowShowing", "Now Showing Title" },
   { "/Recording/Complete", "Now Showing Title" },
   { "/Recording/NowShowingByBucketTitle", "Now Showing Title" },
   { "", "" }
};

struct sfileParts
{
   int      fileNo;
   int64_t  fileSize;
   int      chunks;
};

char lastFsid[ 80 ] = "";
int filePart = -1;
struct sfileParts fileParts[ 255 ];
int filePartsTotal = 0;
static int64_t vstream_totalSize = 0;

int vstream_startstream( char *fsid )
{
   struct mfs_obj_attr *obj;
   int                 pcount;
   int                 index;
   int                 ifsid;
   char                calcFsid[ 80 ];
   unsigned char       *buffer;
   int                 pesFileId;
   int                 size;
   int                 count;
   char                myfsid[ 80 ];
   char                *mypart;
   

   if ( strcmp( fsid, lastFsid ) != 0 )
   {
      strcpy( myfsid, fsid );
      mypart = strchr( myfsid, '/' );
      if ( mypart != 0 )
      {
         *mypart = 0;
         mypart++;
         filePart = atoi( mypart );
      }

      ifsid = vstream_mfs_resolve( myfsid );
      if ( ifsid > 0 )
      {
         vstream_totalSize = 0;
         obj = query_object( ifsid, "Part", &pcount );
         for( index = 0 ; index < pcount ; index ++ )
         {
            sprintf( calcFsid, "Part/%d/File", obj[ index ].subobj );
            fileParts[ index ].fileNo = vstream_query_int( ifsid, calcFsid );
				// HACK - Ignore last chunk of a Part File
				// Why?  I have no idea.
            fileParts[ index ].fileSize = 
               vstream_mfs_fsid_size( fileParts[ index ].fileNo ) - 0x20000;
            fileParts[ index ].chunks = 
               ( fileParts[ index ].fileSize / CHUNKSIZE );
         }
         free( obj );
         filePartsTotal = pcount;

         if ( filePart != -1 )
         {
            if ( filePart > filePartsTotal )
            {
               vstream_error("vstream_fsidtoparts(): Invalid Part File %d\n", filePart);
               return( 1 );
            }
            else
            {
               fileParts[ 0 ].fileNo = fileParts[ filePart ].fileNo;
               fileParts[ 0 ].fileSize = fileParts[ filePart ].fileSize;
               fileParts[ 0 ].chunks = fileParts[ filePart ].chunks;
               filePartsTotal = 1;
            }
         }

         for( index = 0 ; index < filePartsTotal ; index++ )
         {
            vstream_totalSize += fileParts[ index ].fileSize;
         }

         // We have to go into the last chunk to figure its exact size
         buffer = malloc( CHUNKSIZE );
         count = vstream_mfs_fsid_pread( fileParts[ filePartsTotal - 1 ].fileNo, 
            buffer, 0, CHUNKSIZE );
         if ( count == CHUNKSIZE )
         {
            pesFileId = buffer[ 0x00 ] << 24 | buffer[ 0x01 ] << 16 | 
               buffer[ 0x02 ] << 8 | buffer[ 0x03 ];
            if ( pesFileId == 0xf5467abd )
            {
               vstream_totalSize -= fileParts[ filePartsTotal - 1 ].fileSize;
               size = buffer[ 0x0c ] << 24 | buffer[ 0x0d ] << 16 | 
                  buffer[ 0x0e ] << 8 | buffer[ 0x03 ];
               size /= 256;
               size -= 4;
               size *= CHUNKSIZE;
               fileParts[ filePartsTotal - 1 ].fileSize = size;
               fileParts[ filePartsTotal - 1 ].chunks = ( size / CHUNKSIZE );
               vstream_totalSize += size;
            }
         }
         free( buffer );

         strcpy( lastFsid, fsid );
      }
      else
      {
         filePartsTotal = 0;
      }
   }
   return( 0 );
}

int64_t vstream_streamsize( )
{
   return( vstream_totalSize );
}


void vstream_fsidtooffset( int chunk, int *fileNo, int64_t *fileChunk )
{
   int index;

   *fileNo = 0;
   *fileChunk = 0;

   for( index = 0 ; index < filePartsTotal ; index++ )
   {
      if ( chunk >= fileParts[ index ].chunks )
      {
         chunk -= fileParts[ index ].chunks;
      }
      else
      {
         break;
      }
   }
   if ( chunk < fileParts[ index ].chunks )
   {
      *fileNo = fileParts[ index ].fileNo;
      *fileChunk = chunk;
   }
}

int vstream_load_chunk( char *fsid, unsigned char *buff, int size, int64_t foffset )
{
   int      count;
   int64_t  chunk;
   int64_t  fileoffset;
   int      fileNo;
   int64_t  fileNoChunkOffset;
   int64_t  offset;

   vstream_mfs_readahead( 1 );
   if ( vstream_startstream( fsid ) == 1 )
   {
      return( 0 );
   }
   if ( filePartsTotal <= 0 )
   {
      return( 0 );
   }
   if ( foffset >= vstream_streamsize() )
   {
      return( 0 );
   }

   chunk = foffset / CHUNKSIZE;
   offset = foffset - ( chunk * CHUNKSIZE );
   vstream_fsidtooffset( chunk, &fileNo, &fileNoChunkOffset );
   fileoffset = ( fileNoChunkOffset * CHUNKSIZE ) + offset;
   count = vstream_mfs_fsid_pread( fileNo, buff, fileoffset, size );
    
   return( count );
}

void vstream_list_streams( int longList )
{
   struct mfs_dirent   *dir;
   struct mfs_obj_attr *obj;
   u32                 count;
   u32                 h;
   u32                 i;
   int                 pcount;
   time_t              recTime;
   struct tm           *recTimetm;

   if (vstream_start()) return;

   h = 0;
   while( strlen( dirList[ h ].dirName ) > 0 )
   {
      dir = mfs_dir( vstream_mfs_resolve( dirList[ h ].dirName ), &count );
      for ( i = 0 ; i < count ; i++ ) 
      {
         obj = query_object( dir[ i ].fsid, "Part", &pcount );
         free( obj );

         recTime = ( vstream_query_int( dir[ i ].fsid, "StartDate" ) * 86400 );
         recTime += vstream_query_int( dir[ i ].fsid, "StartTime" ) + 60;
         recTimetm = localtime( &recTime );
         recTimetm->tm_year %= 100;

         vstream_error( "%d", vstream_query_int( dir[ i ].fsid, "Part" ) );
         vstream_error( " -- " );
         vstream_error( "%2.2d/%2.2d/%2.2d", recTimetm->tm_mon + 1, recTimetm->tm_mday, recTimetm->tm_year );
         vstream_error( " " );
         vstream_error( "%2.2d:%2.2d", recTimetm->tm_hour, recTimetm->tm_min );
         vstream_error( " -- " );
         vstream_error( "%02d", pcount );
         vstream_error( " -- " );
         vstream_error( "%s", vstream_query_string( dir[ i ].fsid, "Showing/Station/CallSign" ) );
         vstream_error( "\n" );
         if (longList) {
            vstream_error("\t%s -- ", vstream_query_string( dir[ i ].fsid, "Showing/Program/Title" ));
            vstream_error("%s\n", vstream_query_string( dir[ i ].fsid, "Showing/Program/EpisodeTitle" ));
         }
      }
      if ( dir ) vstream_mfs_dir_free( dir );
      h++;
   }
}

int vstream_start()
{
   if (load_super()) return 1;
   if (load_zones()) return 1;
   return 0;
}

