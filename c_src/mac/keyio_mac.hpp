#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <thread>
#include <map>
#include <mach/mach_error.h>
#include <nanomsg/nn.h>
#include <nanomsg/pipeline.h>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <chrono>
#include <stdio.h>
 
int init_sink(void);
int exit_sink(void);

/*
 * Key event information that's shared between C++ and Haskell.
 *
 * type: represents key up or key down
 * page: represents IOKit usage page
 * usage: represents IOKit usage
 */
struct KeyEvent {
    uint64_t type;
    uint32_t page;
    uint32_t usage;
};

/*
 * These are needed to receive unaltered key events from the OS.
 */
static std::thread thread;
static std::thread backdoor_thread;
static CFRunLoopRef listener_loop;
static std::map<io_service_t,IOHIDDeviceRef> source_device;
static int fd[2];
static char *prod = nullptr;
std::ofstream log_file;

void print_iokit_error(const char *fname, int freturn = 0) {
    std::cerr << fname << " error";
    if(freturn) {
        //std::cerr << " " << std::hex << freturn;
        std::cerr << ": ";
        std::cerr << mach_error_string(freturn);
    }
    std::cerr << std::endl;
}

//void __input_backdoor() {
//  char *fifo = "/Users/rocky/.kmonad_fifo";
//
//  unlink(fifo);
//  mkfifo(fifo, 0666);
//  std::ifstream infile(fifo);
//
//  uint32_t type;
//  uint64_t page;
//  uint64_t usage;
//
//  while (1) {
//    if (infile >> type >> page >> usage) {
//      struct KeyEvent e;
//
//      e.type = type;
//      e.page = page; 
//      e.usage = usage;
//
//      write(fd[1], &e, sizeof(struct KeyEvent));
//    }
//  }
//}

void input_backdoor() {
  char *fifo = "/Users/rocky/.kmonad_fifo";

  char buf[1000];

  std::string::size_type sz = 0;

  unlink(fifo);
  mkfifo(fifo, 0777);
  printf("created fifo\n");
  printf("created fifo\n");
  printf("created fifo\n");
  printf("created fifo\n");
  printf("created fifo\n");
  printf("created fifo\n");
  printf("created fifo\n");
  printf("created fifo\n");

  FILE *ptr = fopen(fifo, "r");
  printf("fopen fifo\n");

  uint32_t type;
  uint64_t page;
  uint64_t usage;
  int count = 0;
  std::string delimiter = ",";

  while (1) {
    while(fgets(buf, sizeof(buf), ptr)) {
      size_t pos = 0;
      std::string token;
      std::string s = std::string(buf);

      pos = s.find(delimiter);
      token = s.substr(0, pos);

      type = std::stoul(token,&sz,0);

      s.erase(0, pos + delimiter.length());

      pos = s.find(delimiter);
      token = s.substr(0, pos);

      page = std::stoull(token,&sz,0);

      s.erase(0, pos + delimiter.length());

      pos = s.find(delimiter);
      token = s.substr(0, pos);

      usage = std::stoull(token,&sz,0);

      s.erase(0, pos + delimiter.length());

      struct KeyEvent e;

      e.type = type;
      e.page = page; 
      e.usage = usage;

      write(fd[1], &e, sizeof(struct KeyEvent));
      //count++;
      //printf("type: %" PRIu32 "\n", type);
      //printf("page: %" PRIu64 "\n", page);
      //printf("usage: %" PRIu64 "\n\n", usage);

      buf[0] = '\0';
    }

    rewind(ptr);
  }
}


/*
 * We'll register this callback to run whenever an IOHIDDevice
 * (representing a keyboard) sends input from the user.
 *
 * It passes the relevant information into a pipe that will be read
 * from with wait_key.
 */
void input_callback(void *context, IOReturn result, void *sender, IOHIDValueRef value) {
  struct KeyEvent e;
  IOHIDElementRef element = IOHIDValueGetElement(value);
  e.type = IOHIDValueGetIntegerValue(value);
  e.page = IOHIDElementGetUsagePage(element);
  e.usage = IOHIDElementGetUsage(element);
  log_file << "type: " << e.type << "page: " << e.page << "usage: " << e.usage << "\n";
  write(fd[1], &e, sizeof(struct KeyEvent));
}

void open_matching_devices(char *product, io_iterator_t iter) {
  io_name_t name;
  kern_return_t kr;
  CFStringRef cfproduct = NULL;
  if(product) {
    cfproduct = CFStringCreateWithCString(kCFAllocatorDefault, product, CFStringGetSystemEncoding());
    if(cfproduct == NULL) {
      print_iokit_error("CFStringCreateWithCString");
      return;
    }
  }
  CFStringRef cfkarabiner = CFStringCreateWithCString(kCFAllocatorDefault, "Karabiner VirtualHIDKeyboard", CFStringGetSystemEncoding());
  if(cfkarabiner == NULL) {
    print_iokit_error("CFStringCreateWithCString");
    if(product) {
      CFRelease(cfproduct);
    }
    return;
  }
  for(mach_port_t curr = IOIteratorNext(iter); curr; curr = IOIteratorNext(iter)) {
    CFStringRef cfcurr = (CFStringRef)IORegistryEntryCreateCFProperty(curr, CFSTR(kIOHIDProductKey), kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if(cfcurr == NULL) {
      print_iokit_error("IORegistryEntryCreateCFProperty");
      continue;
    }
    bool match = (CFStringCompare(cfcurr, cfkarabiner, 0) != kCFCompareEqualTo);
    if(product) {
      match = match && (CFStringCompare(cfcurr, cfproduct, 0) == kCFCompareEqualTo);
    }
    CFRelease(cfcurr);
    if(!match) continue;
    IOHIDDeviceRef dev = IOHIDDeviceCreate(kCFAllocatorDefault, curr);
    source_device[curr] = dev;
    log_file.open("/tmp/kek/log");
    IOHIDDeviceRegisterInputValueCallback(dev, input_callback, NULL);
    kr = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeSeizeDevice);
    if(kr != kIOReturnSuccess) {
      print_iokit_error("IOHIDDeviceOpen", kr);
    }
    IOHIDDeviceScheduleWithRunLoop(dev, listener_loop, kCFRunLoopDefaultMode);
  }
  if(product) {
    CFRelease(cfproduct);
  }
  CFRelease(cfkarabiner);
}

/*
 * We'll register this callback to run whenever an IOHIDDevice
 * (representing a keyboard) is connected to the OS
 *
 */
void matched_callback(void *context, io_iterator_t iter) {
  char *product = (char *)context;
  open_matching_devices(product, iter);
}

/*
 * We'll register this callback to run whenever an IOHIDDevice
 * (representing a keyboard) is disconnected from the OS
 *
 */
void terminated_callback(void *context, io_iterator_t iter) {
  for(mach_port_t curr = IOIteratorNext(iter); curr; curr = IOIteratorNext(iter)) {
    source_device.erase(curr);
  }
}

/*
 * Reads a new key event from the pipe, blocking until a new event is
 * ready.
 */
extern "C" int wait_key(struct KeyEvent *e) {
  return read(fd[0], e, sizeof(struct KeyEvent)) == sizeof(struct KeyEvent);
}

/*
 * For each keyboard, registers an asynchronous callback to run when
 * new input from the user is available from that keyboard. Then
 * sleeps indefinitely, ready to received asynchronous callbacks.
 */

void monitor_kb(char *product) {
  kern_return_t kr;
  CFMutableDictionaryRef matching_dictionary = IOServiceMatching(kIOHIDDeviceKey);
  if(!matching_dictionary) {
    print_iokit_error("IOServiceMatching");
    return;
  }
  UInt32 value;
  CFNumberRef cfValue;
  value = kHIDPage_GenericDesktop;
  cfValue = CFNumberCreate( kCFAllocatorDefault, kCFNumberSInt32Type, &value );
  CFDictionarySetValue(matching_dictionary, CFSTR(kIOHIDDeviceUsagePageKey), cfValue);
  CFRelease(cfValue);
  value = kHIDUsage_GD_Keyboard;
  cfValue = CFNumberCreate( kCFAllocatorDefault, kCFNumberSInt32Type, &value );
  CFDictionarySetValue(matching_dictionary,CFSTR(kIOHIDDeviceUsageKey),cfValue);
  CFRelease(cfValue);
  io_iterator_t iter = IO_OBJECT_NULL;
  CFRetain(matching_dictionary);
  kr = IOServiceGetMatchingServices(kIOMasterPortDefault,
      matching_dictionary,
      &iter);
  if(kr != KERN_SUCCESS) {
    print_iokit_error("IOServiceGetMatchingServices", kr);
    return;
  }
  listener_loop = CFRunLoopGetCurrent();
  open_matching_devices(product, iter);
  IONotificationPortRef notification_port = IONotificationPortCreate(kIOMasterPortDefault);
  CFRunLoopSourceRef notification_source = IONotificationPortGetRunLoopSource(notification_port);
  CFRunLoopAddSource(listener_loop, notification_source, kCFRunLoopDefaultMode);
  CFRetain(matching_dictionary);
  kr = IOServiceAddMatchingNotification(notification_port,
      kIOMatchedNotification,
      matching_dictionary,
      matched_callback,
      product,
      &iter);
  if(kr != KERN_SUCCESS) {
    print_iokit_error("IOServiceAddMatchingNotification", kr);
    return;
  }
  for(mach_port_t curr = IOIteratorNext(iter); curr; curr = IOIteratorNext(iter)) {}
  kr = IOServiceAddMatchingNotification(notification_port,
      kIOTerminatedNotification,
      matching_dictionary,
      terminated_callback,
      NULL,
      &iter);
  if(kr != KERN_SUCCESS) {
    print_iokit_error("IOServiceAddMatchingNotification", kr);
    return;
  }
  for(mach_port_t curr = IOIteratorNext(iter); curr; curr = IOIteratorNext(iter)) {}
  CFRunLoopRun();
  for(std::pair<const io_service_t,IOHIDDeviceRef> p: source_device) {
    kr = IOHIDDeviceClose(p.second,kIOHIDOptionsTypeSeizeDevice);
    if(kr != KERN_SUCCESS) {
      print_iokit_error("IOHIDDeviceClose", kr);
    }
  }
}

/*
 * Opens and seizes input from each keyboard device whose product name
 * matches the parameter (if NULL is received, then it opens all
 * keyboard devices). Spawns a thread to receive asynchronous input
 * and opens a pipe for this thread to send key event data to the main
 * thread.
 *
 * Loads a the karabiner kernel extension that will send key events
 * back to the OS.
 */
extern "C" int grab_kb(char *product) {
  // Source
  if (pipe(fd) == -1) {
    std::cerr << "pipe error: " << errno << std::endl;
    return errno;
  }
  if(product) {
    prod = (char *)malloc(strlen(product) + 1);
    strcpy(prod, product);
  }
  printf("before initialize threads\n");
  backdoor_thread = std::thread{input_backdoor};
  thread = std::thread{monitor_kb, prod};
  // Sink
  return init_sink();
}

/*
 * Releases the resources needed to receive key events from and send
 * key events to the OS.
 */
extern "C" int release_kb() {
  int retval = 0;
  kern_return_t kr;
  // Source
  if(thread.joinable()) {
    CFRunLoopStop(listener_loop);
    thread.join();
  } else {
    std::cerr << "No thread was running!" << std::endl;
  }
  if(prod) {
    free(prod);
  }
  if (close(fd[0]) == -1) {
    std::cerr << "close error: " << errno << std::endl;
    retval = 1;
  }
  if (close(fd[1]) == -1) {
    std::cerr << "close error: " << errno << std::endl;
    retval = 1;
  }
  // Sink
  if(exit_sink()) retval = 1;
  return retval;
}
