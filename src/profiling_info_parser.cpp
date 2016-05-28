#include <iostream>
#include <sstream>
#include "info_file_io.h"

using namespace std;

int str_to_int (string str) {
  stringstream ss(str);
  int num;
  if((ss >> num).fail())
  {
      cout<<"\nnot a valid record size"<<endl;
  }
  return num;
}

void show_usage() {
    //TODO add usage details
    // sample: ./bin_info_parser a testInfo testAccesses a 10 >parsed_info

    cout<<"\nUsage: "<<"add details"<<endl;
}

int main(int argc, char* argv[]) {
    char output_mode;
    string obj_info, obj_accesses, obj_class, obj_record;
    int max_record_size = 0;
    
    if(argc > 1) {
        if(argc >6)
            show_usage();
        else {
            // a: info  & accesses, i: info only
            output_mode = *argv[1];
            
            // profiling info file
            obj_info.assign(argv[2]);
            
            // profiling accesses file
            obj_accesses.assign(argv[3]);
            
            // if you want to show only one class, then give its name,
            // if all then pass 'a'
            obj_class.assign(argv[4]);
            
            // to limit the output of thread accesses list.
            max_record_size = str_to_int(obj_record.assign(argv[5]));

            profiling_io::set_output_mode(output_mode);
            profiling_io::set_max_record_size(max_record_size);
            profiling_io::set_object_class(obj_class);
            profiling_io::change_profiling_files(&obj_info, &obj_accesses);
            profiling_io::output_shared_objects_info();
            }
    }
    else {
        show_usage();
    }
    return 0;
}
