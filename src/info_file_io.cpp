#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ios>
#include <list>
#include <map>
#include <string.h>
#include "jvmti.h"
#include "info_file_io.h"

using namespace std;

struct object_info_record {
  char* object_class;
  jlong* thread_accesses;
  int accesses_length;
};

map<jlong, object_info_record> shared_objects;

ofstream profiling_writer;
ofstream object_info_writer;
ifstream profiling_reader;

char* object_info_file = "ObjectInfo";
char* object_accesses_file = "ObjectAccesses";


char io_mode;
int max_record_size;
string object_class;
const string all_objects = "a";
namespace profiling_io {

    void set_output_mode(char mode){
        io_mode = mode;
    }

    void set_max_record_size(int size) {
        max_record_size = size;
    }

    void set_object_class(string object_class_str) {
        object_class.assign(object_class_str);
    }
    
    void change_profiling_files(string* info, string* accesses) {

        object_info_file = new char[(info->length() + 1)*sizeof(char)];;
        info->copy(object_info_file, info->length());
        object_info_file[info->size()] = '\0';

        object_accesses_file = new char[(accesses->length() + 1)*sizeof(char)];;
        accesses->copy(object_accesses_file, accesses->length());
        object_accesses_file[accesses->size()] = '\0';

    }

    void set_write_buffer_size(int size) {
        // Not sure how to control that.
    }

    void open_write(void) {
        if(object_accesses_file && object_info_file) {
            profiling_writer.open(
                object_accesses_file, ios::out  | ios::trunc |ios::binary);
            object_info_writer.open(
                object_info_file, ios::out | ios::trunc | ios::binary);
        }
        else {
            profiling_writer.open(
                "ObjectInfo", ios::out  | ios::trunc |ios::binary);
            object_info_writer.open(
                "ObjectAccesses", ios::out | ios::trunc | ios::binary);
        }
    }
    
    void close_write(void) {
        profiling_writer.close();
        object_info_writer.close();
    }

    void write_access_info(jlong object_ID, list<jlong>* access_seq) {
        
        int record_size = access_seq->size()*sizeof(jlong);

        profiling_writer.write((char*)&record_size, sizeof(int));
        profiling_writer.write((char*)&object_ID, sizeof(jlong));

        for(list<jlong>::const_iterator it = access_seq->begin();
            it != (access_seq->end()); ++it) {
            profiling_writer.write((char*)&(*it),sizeof(jlong));
        }
    }

    void write_object_info(jlong object_ID,
                           jlong object_size,
                           char* object_class) {
        
        int record_size = strlen(object_class)*sizeof(char);

        object_info_writer.write((char*)&(record_size),sizeof(int));
        object_info_writer.write((char*)&object_ID,sizeof(jlong));
        object_info_writer.write((char*)&(*object_class),record_size);
    }

    void read_objects_class() {
        profiling_reader.open(object_info_file);
        
        if(profiling_reader.fail()) {
            cout<<"Could not open Object Info file!"<<endl;
            exit(1);
        }
        
        profiling_reader.peek();
        if(profiling_reader.eof()){
            cout<<"No shared objects were found"<<endl;
            profiling_reader.close();
            return;
        }

        int record_size = 0;
        jlong object_ID = -1;

           do{
           // size of the class signature (string terminator not included!)
           profiling_reader.read((char*)&(record_size), sizeof(int));

           // object id
           profiling_reader.read((char*)&(object_ID), sizeof(jlong));

           // class signature:
           shared_objects[object_ID].object_class =
                   (char*) malloc(record_size + sizeof(char));
           profiling_reader.read(
                shared_objects[object_ID].object_class,record_size);
           (*(shared_objects[object_ID].object_class+record_size)) = '\0';

           profiling_reader.peek();
        }while(!(profiling_reader.eof()));
        profiling_reader.close();
    }

    void read_objects_accesses() {
        profiling_reader.open(object_accesses_file);

         if(profiling_reader.fail()) {
            cout<<"Could not open Accesses file!"<<endl;
            exit(1);
        }

        profiling_reader.peek();
        if(profiling_reader.eof()){
            cout<<"No shared objects were found"<<endl;
            profiling_reader.close();
            return;
        }

        int record_size = 0;
        jlong object_ID = -1;

        do {
           // size of the accesses array
           profiling_reader.read((char*)&(record_size), sizeof(int));

           // object id
           profiling_reader.read((char*)&(object_ID), sizeof(jlong));

           int arr_length = record_size/sizeof(jlong);
           int read_length = (arr_length<max_record_size?
                                arr_length:max_record_size);
           int skip_length = arr_length-read_length;

           shared_objects[object_ID].thread_accesses = new jlong[read_length];

           for(int i = 0; i< read_length; ++i) {
               profiling_reader.read((char*)&(
                       shared_objects[object_ID].thread_accesses[i]),
                       sizeof(jlong));
           }
           profiling_reader.seekg(skip_length*sizeof(jlong), ios_base::cur);
           shared_objects[object_ID].accesses_length = arr_length;

           profiling_reader.peek();
        } while(!(profiling_reader.eof()));

        profiling_reader.close();
    }

    void output_object_info(){
        typedef map<jlong, object_info_record> MapType;
        MapType::const_iterator end = shared_objects.end();

        for(MapType::const_iterator it = shared_objects.begin();
            it != end; ++it) {
            // TODO this is the easy, dumb, memory consuming way of filtering,
            // do it right later, put only what is needed in the map.
            if(object_class.compare(all_objects) == 0 ||
               object_class.compare(it->second.object_class) == 0) {
                cout<<it->second.object_class<<endl;
            }
        }
    }

    void output_accesses() {
        typedef map<jlong, object_info_record> MapType;
        MapType::const_iterator end = shared_objects.end();
        
        for(MapType::const_iterator it = shared_objects.begin(); 
            it != end; ++it) {

            // TODO Again the dumb way, fix later
            if(object_class.compare(all_objects) == 0 ||
               object_class.compare(it->second.object_class) == 0) {
                cout<<it->second.object_class
                    <<it->second.accesses_length<<": ";

                int arr_length = it->second.accesses_length;
                int read_length = (arr_length<max_record_size?
                                arr_length:max_record_size);
                for(int i = 0; i<read_length; i++) {
                cout<<it->second.thread_accesses[i]<<"  ";
            }
            cout<<endl;
            }
        }
    }

    void output_shared_objects_info() {
        cout<<"\nShared Objects' Details:"<<endl;
        read_objects_class();

        //TODO make these constants
        if(io_mode == 'i')  {
            output_object_info();

        }
        else if(io_mode == 'a') {
            read_objects_accesses();
            output_accesses();
        }      
    }
};


