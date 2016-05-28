/* 
 * File:   info_file_io.h
 * Author: nosheen
 *
 * Created on September 26, 2011, 4:56 PM
 */
#include <list>

#include "jvmti.h"
#ifndef INFO_FILE_IO_H
#define	INFO_FILE_IO_H

using namespace std;
#ifdef	__cplusplus
extern "C" {
#endif
namespace profiling_io {
    void set_output_mode(char mode);
    void set_max_record_size(int size);
    void set_object_class(string object_class_str);
    void change_profiling_files(string* info, string* accesses);
    void set_write_buffer_size(int size);
    void open_read(void);
    void open_write(void);
    void close_read(void);
    void close_write(void);
    void write_access_info(jlong object_ID, list<jlong>* access_seq);
    void write_object_info(jlong object_ID,jlong object_size,char* object_class);
    void output_shared_objects_info(void);
};

#ifdef	__cplusplus
}
#endif

#endif	/* INFO_FILE_IO_H */

