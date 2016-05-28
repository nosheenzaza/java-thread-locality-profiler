 /*
  * thread_Locality_info
  *
  * Purpose: A profiling agent than can be plugged to JVM to monitor field
  * accesses and modifications by threads.
  *
  * Usage: Please refer to the included Makefile for compilation information,
  * and for a sample usage case, refer to run_tests_with_agent.sh
  *
  * Author: Nosheen Zaza
  */

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <list>
#include <set>
#include <time.h>
#include "jvmti.h"
#include "info_file_io.h"

//#define DEBUG
//#define DEBUG_SHARED

using namespace std;

/******************************************************************************/
/* Debugging stuff                                                            */
/******************************************************************************/

#ifdef DEBUG_SHARED
string *method_name = NULL;
string *field_name = NULL;
#endif


/******************************************************************************/
/* Declerations                                                               */
/******************************************************************************/

/*
 * This lock ensures that only one thread will have access to critical sections
 * in code.
 */
jrawMonitorID lock;

/*
 * To make deletion more effecient, we use this flag so we do not delete
 * elements from the set if the program is terminating.
 */
bool program_running = true;

/*
 * Special values for thread id, meaning no thread id was set before. Used as an
 * initial value.
 */
const jlong NO_THREAD = -2;

/*
 * a structure that holds information about an object's thread locality.
 * TODO update this as you go!
 */
struct thread_access_info {
    jlong object_ID;
    bool is_thread_local : 1;
    /*
     * When the object is local, thread_ID holds the ID of the only thread
     * that touched the object. When it is shared, accesses holds the IDs of
     * the sequence of threads accessing an object.
     */
    union {
    jlong thread_ID;
    list<jlong> *accesses;
    };
};

/* To make things look a bit nicer */
typedef struct thread_access_info *ThreadAccessInfo;

struct thread_info {
    jlong thread_ID;
    string* thread_name;
};

list<thread_info> thread_names;

/*
 * A set used to track all shared objects, so we can collect their information
 * upon terminateion.
 */
set<jlong> shared_objects_tags;



/******************************************************************************/
/* Counters to generate statistics                                            */
/******************************************************************************/

/* Number of shared objects */
jlong shared_objects_count = 0;

/* Number of shared objects due to being touched by the garbage collector */
jlong gc_shared_objects_count = 0;

/* Number of shared objects due to being touched by the finalizer */
jlong finalizer_shared_objects_count = 0;

/* Total number of objects tagged and profiled */
jlong total_objects_count = 0;

/* Total memory occupied by objects tagged and profiled in bytes */
jlong total_objects_memory = 0;

/*
 * Total memory occupied by objects shared in bytes. This does not include the
 * objects marked as GC shared or finalizer shared.
 */
jlong shared_objects_memory = 0;

/* Memory occupied byt the JVM objects (does not include native code memory)*/
jlong vm_objects_memory = 0;

/******************************************************************************/
/* Helper functions                                                           */
/******************************************************************************/

/* Given a thread reference, obtain the name of the thread */
static string get_thread_name(jthread thread, jvmtiEnv* jvmti_env) {

    jvmtiThreadInfo thread_info;

    jvmti_env->GetThreadInfo(thread, &thread_info);

    string thread_name = "?";

    if (thread_info.name) {
        thread_name.assign(thread_info.name);
    }
    return thread_name;
}

/* Given a field and field class, obtain the field's name */
static string get_field_name(jfieldID field,
                             jclass fieldklass,
                             jvmtiEnv* jvmti_env) {

    string field_name, class_name;
    char *fName, *cName;

    jvmti_env->GetClassSignature(fieldklass, &cName, NULL);
    jvmti_env->GetFieldName(fieldklass, field, &fName, NULL,NULL);

    if(cName) {
         class_name.assign(cName);
            jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(cName));
    }

    if(fName) {
         field_name.assign(fName);
            jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(fName));
    }

    return class_name + field_name;
}

/* Outputs the name of a field and the thread accessing it */
static void output_field_info(jfieldID field,
                              jclass fieldklass,
                              jthread thread,
                              jvmtiEnv* jvmti_env) {
    cout << "touching field : " << get_field_name(field, fieldklass, jvmti_env)
         << " by thread: " << get_thread_name(thread, jvmti_env) << endl;
}

/* returns the name and signature of a method*/
static string get_method_info(jmethodID method,
                               jvmtiEnv* jvmti_env) {
    string method_info = "?";
    char *methodName, *methodSignature;
    jvmti_env->GetMethodName(method, &methodName, &methodSignature, NULL);

    if (methodName && methodSignature) {

        method_info.assign(methodName);
        method_info.append(methodSignature);
        jvmti_env->
            Deallocate(reinterpret_cast<unsigned char*> (methodName));
        jvmti_env->
            Deallocate(reinterpret_cast<unsigned char*> (methodSignature));

    }
    return method_info;
}

/* outputs the name and signature of a method and the thread accessing it */
static void output_method_info(jmethodID method,
                               jthread thread,
                               jvmtiEnv* jvmti_env) {
    char *methodName, *methodSignature;
    jvmti_env->GetMethodName(method, &methodName, &methodSignature, NULL);

    if (methodName && methodSignature) {
        cout << "Entering method " << methodName << methodSignature
             << " from thread: " << get_thread_name(thread, jvmti_env) << endl;

        jvmti_env->
            Deallocate(reinterpret_cast<unsigned char*> (methodName));
        jvmti_env->
            Deallocate(reinterpret_cast<unsigned char*> (methodSignature));
    }
}

/* output an execution summary */
void output_result() {
    cout<< "\nTotal number of objects touched: "
        << total_objects_count
        << endl
        << "\nNumber of local objects: "
        << (total_objects_count-shared_objects_count)
        << " ("
        <<((total_objects_count-shared_objects_count)*100/
           (double)total_objects_count)<<"%)"
        << endl
        << "Number of shared objects: "
        << (shared_objects_count)
        << " ("
        << (shared_objects_count*100/(double)total_objects_count)<<"%)"
        << endl
        << "Number of objects marked shared due to garbage collection: "
        << gc_shared_objects_count
        << " (" <<(gc_shared_objects_count*100/(double)total_objects_count)<<"%)"
        << endl
        << "Number of objects marked shared due to finalization:"
        << finalizer_shared_objects_count
        << " (" <<(finalizer_shared_objects_count*100/(double)total_objects_count)<<"%)"
        << endl
        << "\nTotal touched objects memory occupied in bytes: "
        << total_objects_memory
        << endl  
        << "Shared memory of touched objects occupied in bytes: "
        << shared_objects_memory
        << " (" <<(shared_objects_memory*100/(double)total_objects_memory)<<"%)"
        << endl;

     cout << "\nThread IDs and Names (During live phase): "<< endl;
     list<thread_info>::const_iterator it;
     for(it = thread_names.begin(); it!= thread_names.end(); ++it) {
         cout<<(*it).thread_ID<<": "<<(*(*it).thread_name)<<endl;
     }
}

/******************************************************************************/
/* Object Tags and information                                                */
/******************************************************************************/

/*
 * Returns the value of an object's tag, if an error occurs while retrieving
 * the tag, -1 is returned. A tag value of 0 indicated that the object was was
 * not tagged before.
 */
static jlong get_tag(jobject object, jvmtiEnv* jvmti_env) {

    jlong tag_value = -1; //holds current tag value of object initialized to -1.

    /*
     * According to documentation of field acces & modification:
     * "Object with the field being accessed if the field is an instance field;
     * NULL otherwise".
     */

    jvmti_env->GetTag(object, &tag_value);

    return tag_value;
}

/*
 * Create a unique id for each object, and create an initial access info
 * structure for it.
 */
static ThreadAccessInfo create_object_info(jobject object,
                                           jlong thread_ID,
                                           jvmtiEnv* jvmti_env) {

    /*
     *  We need this mainly for thread objects, since we need a way to identify
     * threads. For now I stupidly put tags for all objects types.
     */
    static jlong id_generator = 1;

    //Set a unique identifier for the object and init its info.
    ThreadAccessInfo access_info =
            (ThreadAccessInfo) malloc(sizeof(struct thread_access_info));
    access_info->object_ID = id_generator;
    access_info->is_thread_local = true;
    access_info->thread_ID = thread_ID;

    jlong obj_size = -1;
    jvmti_env->GetObjectSize(object, &obj_size);

    total_objects_memory += obj_size;
    ++total_objects_count;

    // new id for the next object with no tag.
    ++id_generator;

    return access_info;
}

/*
 * Uses the tag of an object as a pointer to a structure that holds information
 * about that object's thread locality. recieves recent information about
 * an object, then either creates a record for the object or updates
 * its existing record.
 * TODO right the procedural checks applied by this function
 */
static void update_object(jobject object,
                          jthread thread,
                          JNIEnv* jni_env,
                          jvmtiEnv* jvmti_env) {

    /*
     * We restict tracking object to the live phase only, better perfomance,
     * less errors, and I think we this way track the more intersting results
     * only.
     */
    jvmtiPhase currentPhase;
    jvmti_env->GetPhase(&currentPhase);
    if(currentPhase != JVMTI_PHASE_LIVE) return;

    // No point of work if we have no object!
    if(object == NULL) return;

    /*
     * Threads are objects, we need to tag them as well
     * We cannot use a jthread for thread identification, thus we must tag
     * threads. The reason is that jthreads move in memory, jthread is merely
     * an address in memory, thus it cannot be used for identification.
     */
    jlong thread_tag_value = get_tag(thread, jvmti_env);
    jlong object_tag_value = get_tag(object, jvmti_env);

    // Not much we can do about it.
    if(thread_tag_value == -1 || object_tag_value == -1){
        return;
    }

    // the following code assumes that we have a valid tag values

    ThreadAccessInfo thread_as_object_access_info, object_access_info;
    // if a thread is not tagged, tag it and create an info structure for it.
    if (thread_tag_value == 0) {
        thread_as_object_access_info =
                create_object_info(thread, NO_THREAD, jvmti_env);

        // Make the reference to the info structure the tag of the thread.
        jvmtiError err = jvmti_env->SetTag(thread,
                                            reinterpret_cast<jlong>(
                                            thread_as_object_access_info));
#ifdef DEBUG
        if(err != JVMTI_ERROR_NONE )
            cout<<"something went so wrong with tagging a thread!"<<endl;
#endif

    }
    else { // otherwise,retrieve its information
        thread_as_object_access_info =
                reinterpret_cast<ThreadAccessInfo>(thread_tag_value);
    }

    // At this point, thread_as_object_access_info should be properly
    // initialized.

   /*
    *  If the object's tag was not set before, set it and create its initial
    * info.
    */
    if (object_tag_value == 0) {
        // at this point we are sure that the thread is properly tagged
        object_access_info = create_object_info(object,
                thread_as_object_access_info->object_ID, jvmti_env);

      // Make the reference to the info structure the tag of the object.
      jvmti_env->SetTag(object, reinterpret_cast<jlong>(object_access_info));
    }
    else {
        object_access_info =
                reinterpret_cast<ThreadAccessInfo>(object_tag_value);

        if(object_access_info->is_thread_local &&
           object_access_info->thread_ID !=
           thread_as_object_access_info->object_ID) {

            /*
             * If an object was touched only by finalizer, only increment
             * the finalizer shared objects counter.
             */
            if(get_thread_name(thread, jvmti_env).compare("Finalizer") == 0) {
                ++finalizer_shared_objects_count;
                return;
            }

            /*
             * This case occurs when a thread is touching  an object, its
             * access info structure is created, then the thread is
             * accessed by another thread as an object this time. In this case,
             * the thread is local even the previous ID differs from the
             * current ID.
             */
            if(object_access_info->thread_ID == NO_THREAD) {
                object_access_info->thread_ID =
                        thread_as_object_access_info->object_ID;
                return;
            }
#ifdef DEBUG
            cout<<"object with id: "<<object_access_info->object_ID
                << " is shared"<< endl;
#endif
            list<jlong> *threads_seq = new list<jlong>;
            threads_seq->push_back(object_access_info->thread_ID);
            threads_seq->push_back(thread_as_object_access_info->object_ID);
            object_access_info->accesses = threads_seq;

            object_access_info->is_thread_local = false;
            /*
             * We update the number of shared objects and the object status here
             * just in case we lost objects while iterating over the heap
             */
            ++shared_objects_count;

            jlong obj_size = 0;
            jvmti_env->GetObjectSize(object, &obj_size);
            jclass obj_class = jni_env->GetObjectClass(object);
            shared_objects_memory += obj_size;

            char* klass_signature;
            jvmtiError err =
                    jvmti_env->GetClassSignature(
                    obj_class, &klass_signature, NULL);
            
            if(err == JVMTI_ERROR_NONE) {
                profiling_io::write_object_info(object_access_info->object_ID,
                                            obj_size,
                                            klass_signature);
#ifdef DEBUG_SHARED
                cout<<"Class: "klass_signature<<endl;
#endif
            }
            else
                profiling_io::write_object_info(object_access_info->object_ID,
                                            obj_size,
                                            "?");
#ifdef DEBUG_SHARED
             if(field_name != NULL) {
                cout<<object_access_info->accesses->back()<<*field_name<<endl;
            }
            if(method_name != NULL) {
                cout<<object_access_info->accesses->back()<<*method_name<<endl;
            }
#endif

            // Keep a reference to the shared object.
            shared_objects_tags.insert(object_tag_value);
            jvmti_env->
            Deallocate(reinterpret_cast<unsigned char*> (klass_signature));           
        }
        else if(!(object_access_info->is_thread_local)) {
            list<jlong> *threads_seq = object_access_info->accesses;
                if(threads_seq->back() != thread_as_object_access_info->object_ID)
                    threads_seq->push_back(thread_as_object_access_info->object_ID);
        }
    }
#ifdef DEBUG
     cout<< "Object with ID: " << object_access_info->object_ID
         << " was touched by thread "
         << get_thread_name(thread, jvmti_env) << endl;
#endif
}

/*
 * Do the last updates to object info, write them to disk, then free the space
 * occupied by the info.
 */
static void record_object_info(jlong tag) {
    ThreadAccessInfo object_access_info =
                reinterpret_cast<ThreadAccessInfo> (tag);

    if(!(object_access_info->is_thread_local)) {
        list<jlong> *threads_seq = object_access_info->accesses;
        profiling_io::write_access_info(
            object_access_info->object_ID, threads_seq);
        delete threads_seq;
        if(program_running)
            shared_objects_tags.erase(tag);
    }
    free(object_access_info);
}


/******************************************************************************/
/* JVMTI callbacks                                                            */
/******************************************************************************/

/*
 * Set field access and modifictaion watches on all fields of all classes loaded
 * by the JVM. This is needed to be able to recieve field access and
 * modification events, since we cannot do so unless the fields are being
 * watched for such events.
 *
 * NOTES: We ignore static fields and synthetic fields. Synthetic fields are
 * generated by the compiler but not present in the original source code.
 */
void JNICALL cb_class_prepare(  jvmtiEnv *jvmti_env,
                                JNIEnv* jni_env,
                                jthread thread,
                                jclass klass) {

    jint field_number;
    jfieldID *field_IDs;

    jvmti_env->GetClassFields(klass, &field_number, &field_IDs);

    if(field_IDs) {

        for(int i = 0; i < field_number; i++) {

            // if the field is compiler generated, do not watch it.
            jboolean isSynthetic = false;
            jvmti_env->IsFieldSynthetic(klass, field_IDs[i], &isSynthetic);
            if(isSynthetic) continue;

            // if the field is static, do not watch it.
            jint access_flags;
            jvmti_env->GetFieldModifiers(klass, field_IDs[i],&access_flags);

            // I got the 0x0008 bit mask value from the JVM specification doc.
            if(access_flags & 0x0008) {
                continue;
            }

            jvmti_env->SetFieldAccessWatch(klass,field_IDs[i]);
            jvmti_env->SetFieldModificationWatch(klass,field_IDs[i]);
        }
        jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(field_IDs));
    }
}

/*
 * Callback for field access event.
 * Causes an object update.
 */
void JNICALL cb_field_access( jvmtiEnv *jvmti_env,
                              JNIEnv* jni_env,
                              jthread thread,
                              jmethodID method,
                              jlocation location,
                              jclass fieldklass,
                              jobject object,
                              jfieldID field) {
#ifdef DEBUG
     output_field_info(field, fieldklass, thread, jvmti_env);
#endif
     jvmti_env->RawMonitorEnter(lock);

//     if(field_name != NULL)
//          delete field_name;
//     field_name = new string;
//     field_name->assign(get_field_name(field, fieldklass, jvmti_env));

     update_object(object, thread, jni_env, jvmti_env);
      jvmti_env->RawMonitorExit(lock);
}

/*
 * Callback for field modification event.
 * Causes an object update.
 */
void JNICALL cb_field_modification( jvmtiEnv *jvmti_env,
                                    JNIEnv* jni_env,
                                    jthread thread,
                                    jmethodID method,
                                    jlocation location,
                                    jclass fieldklass,
                                    jobject object,
                                    jfieldID field,
                                    char signature_type,
                                    jvalue new_value) {
#ifdef DEBUG
    output_field_info(field, fieldklass, thread, jvmti_env);
#endif
    jvmti_env->RawMonitorEnter(lock);

//     if(field_name != NULL)
//          delete field_name;
//    field_name = new string;
//    field_name->assign(get_field_name(field, fieldklass, jvmti_env));

    update_object(object, thread, jni_env, jvmti_env);
    jvmti_env->RawMonitorExit(lock);
}
/*
 * Callback for method entry event.
 * Causes an object update.
 */
void JNICALL cb_method_entry(jvmtiEnv *jvmti_env,
                             JNIEnv* jni_env,
                             jthread thread,
                             jmethodID method) {

    /*
     * to avoid error 112, method entry can be checked only during the live
     * phase.
     *
     * TODO maybe it is safe to remove this check now, as we do it again
     * when trying to update an object
     */
    jvmtiPhase currentPhase;
    jvmti_env->GetPhase(&currentPhase);
    if(currentPhase != JVMTI_PHASE_LIVE) return;


#ifdef DEBUG
    output_method_info(method, thread, jvmti_env);
#endif
    jvmti_env->RawMonitorEnter(lock);
    /*
     * 'this' resides in slot 0 of the stack frame (at depth 0 also according to
     * my research!).
     */
    
//    if(method_name != NULL)
//        delete method_name;
//    method_name = new string;
//    method_name->assign(get_method_info(method, jvmti_env));

//    jobject current_object;
//    jvmtiError err = jvmti_env->GetLocalObject(thread, 0, 0, &current_object);
//
//    if (current_object != NULL && err == JVMTI_ERROR_NONE) {
//        update_object(current_object, thread, jni_env, jvmti_env);
//    }
    jvmti_env->RawMonitorExit(lock);
}

/*
 * Handle when an object is garbage collected. if it was touched by a thread
 * other than the garbage collector, mark it as gc-shared. We do so to
 * distinguish between objects that are "actually" shared and those shared only
 * when garbage collected.
 *
 */
void JNICALL cb_object_free(jvmtiEnv *jvmti_env, jlong tag) {
    ThreadAccessInfo object_access_info =
            reinterpret_cast<ThreadAccessInfo> (tag);
    /*
     * If the object is not already shared, then touching it by gc mark it gc
     * shared.
     *
     * Note that an object might be shared and GC shared at the same time, but
     * these we do not count as shared due to gc, they are shared due to other
     * threads.
     */
    if (object_access_info->is_thread_local) {
        ++gc_shared_objects_count;
    }
    record_object_info(tag);
}

/*
 * If some strange problems appear, disable this guy & test again!
 */
void JNICALL cb_thread_start(jvmtiEnv *jvmti_env,
                         JNIEnv* jni_env,
                         jthread thread) {

    jvmtiPhase currentPhase;
    jvmti_env->GetPhase(&currentPhase);
    if(currentPhase != JVMTI_PHASE_LIVE) return;

    string name = get_thread_name(thread, jvmti_env);

#ifdef DEBUG
    cout<<"Starting thread: "<<name<<endl;
#endif

    jvmti_env->RawMonitorEnter(lock);

    jlong tag = -1;

    jvmti_env->GetTag(thread, &tag);

    if(tag == -1) {
        jvmti_env->RawMonitorExit(lock);
#ifdef DEBUG
        cout<<"could not fetch thread\'s tag!"<<endl;
#endif
        return;
    }

    ThreadAccessInfo access_info;
    if(tag == 0) {
       access_info = create_object_info(thread, NO_THREAD, jvmti_env);
       jvmti_env->SetTag(thread, reinterpret_cast<jlong>(access_info));
    }
    else
        access_info = reinterpret_cast<ThreadAccessInfo>(tag);

    thread_info thread_inf;
    thread_inf.thread_ID =  access_info->object_ID;
    thread_inf.thread_name = new string();
    thread_inf.thread_name->assign(name);

    thread_names.push_back(thread_inf);
    jvmti_env->RawMonitorExit(lock);
}

/*
 * Register capabilities and sets callbacks for class prepare, method entry,
 * field access and field modification.
 */
void init_jvmti_callbacks(jvmtiEnv* env) {

    jvmtiCapabilities capabilities = { 1 };
    jvmtiEventCallbacks callbacks = { 0 };

    capabilities.can_generate_method_entry_events = 1;
    capabilities.can_generate_field_access_events = 1;
    capabilities.can_generate_field_modification_events = 1;
    capabilities.can_access_local_variables = 1;
    capabilities.can_tag_objects = 1;
    capabilities.can_generate_object_free_events = 1;

    env->AddCapabilities(&capabilities);

    env->SetEventNotificationMode(JVMTI_ENABLE,
            JVMTI_EVENT_CLASS_PREPARE, NULL);
    env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, NULL);
    env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_FIELD_ACCESS, NULL);
    env->SetEventNotificationMode(JVMTI_ENABLE,
            JVMTI_EVENT_FIELD_MODIFICATION, NULL);
    env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_OBJECT_FREE, NULL);
    env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_START, NULL);
    
    callbacks.ClassPrepare = &cb_class_prepare;
    callbacks.MethodEntry = &cb_method_entry;
    callbacks.FieldAccess = &cb_field_access;
    callbacks.FieldModification = &cb_field_modification;
    callbacks.ObjectFree = &cb_object_free;
    callbacks.ThreadStart = &cb_thread_start;
    env->SetEventCallbacks(&callbacks, sizeof(callbacks));
}


/******************************************************************************/
/* Agent loading and unloading                                                */
/******************************************************************************/

/*
 * Measuring runtime stuff
 */
int startTime, endTime;
double totalTime;

void output_user_runtime() {

    int hours = totalTime/3600;
    int mins = (totalTime-(hours*3600))/60;
    int secs = (totalTime-(hours*3600)-(mins*60));

    cout<<"\nRuntime: "<<hours<<"h "<<mins<<"m "<<secs<<"secs"<<endl;
}

void parse_options(char* options ) {
    string *s_options, *object_info_file, *object_accesses_file;


    if(options == NULL) return;

    s_options = new string();
    s_options->assign(options);
    object_info_file = new string();
    object_accesses_file = new string();

    int comma = s_options->find(',');

    object_info_file->assign(s_options->substr(0, comma));
    object_accesses_file->assign(s_options->substr(comma+1,s_options->length()-1));


    profiling_io::change_profiling_files(object_info_file, object_accesses_file);
}

/*
 * Start the agent, open the output file and initialize callbacks.
 */
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    startTime = time(NULL);
    parse_options(options);
    profiling_io::open_write();

    jvmtiEnv* env;
    vm->GetEnv(reinterpret_cast<void**>(&env), JVMTI_VERSION);
    env->CreateRawMonitor("Callbacks Lock", &lock);

    init_jvmti_callbacks(env);
    
    return JNI_OK;
}

/*
 * Before unloading the agent, close the output file and print a summary of
 * profiling results.
 */
JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    program_running = false;
    set<jlong>::iterator it;
    for (it = shared_objects_tags.begin();
            it != shared_objects_tags.end(); it++) {
        record_object_info(*it);
    }
    output_result();
    profiling_io::close_write();

    endTime = time(NULL);
    totalTime = difftime(endTime, startTime);
    output_user_runtime();
}