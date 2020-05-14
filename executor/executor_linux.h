// Copyright 2015 syzkaller project authors. All rights reserved.
// Use of this source code is governed by Apache 2 LICENSE that can be found in
// the LICENSE file.

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
int cover_size;
const unsigned long KCOV_TRACE_PC = 0;
const unsigned long KCOV_TRACE_CMP = 1;
size_t mmap_alloc_size = 30000;
template <typename kernel_u64_t, int N> struct kcov_remote_arg {
  unsigned trace_mode;
  unsigned area_size;
  unsigned num_handles;
  kernel_u64_t common_handle;
  kernel_u64_t handles[N];
};

struct uint64_aligned64 {
  uint64 v;
} __attribute__((aligned(8)));

struct uint64_aligned32 {
  uint64 v;
} __attribute__((packed, aligned(4)));

typedef kcov_remote_arg<uint64_aligned32, 0> kcov_remote_arg32;
typedef kcov_remote_arg<uint64_aligned64, 0> kcov_remote_arg64;

typedef char kcov_remote_arg32_size[sizeof(kcov_remote_arg32) == 20 ? 1 : -1];
typedef char kcov_remote_arg64_size[sizeof(kcov_remote_arg64) == 24 ? 1 : -1];

#define KCOV_INIT_TRACE32 _IOR('c', 1, uint32)
#define KCOV_INIT_TRACE64 _IOR('c', 1, uint64)
#define KCOV_ENABLE _IO('c', 100)
#define KCOV_DISABLE _IO('c', 101)
#define KCOV_REMOTE_ENABLE32 _IOW('c', 102, kcov_remote_arg32)
#define KCOV_REMOTE_ENABLE64 _IOW('c', 102, kcov_remote_arg64)

#define KCOV_SUBSYSTEM_COMMON (0x00ull << 56)
#define KCOV_SUBSYSTEM_USB (0x01ull << 56)

#define KCOV_SUBSYSTEM_MASK (0xffull << 56)
#define KCOV_INSTANCE_MASK (0xffffffffull)

static inline __u64 kcov_remote_handle(__u64 subsys, __u64 inst) {
  if (subsys & ~KCOV_SUBSYSTEM_MASK || inst & ~KCOV_INSTANCE_MASK)
    return 0;
  return subsys | inst;
}

static bool detect_kernel_bitness();

static void os_init(int argc, char **argv, void *data, size_t data_size) {
  prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
  is_kernel_64_bit = detect_kernel_bitness();
  if (mmap(data, data_size, PROT_READ | PROT_WRITE | PROT_EXEC,
           MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0) != data)
    fail("mmap of data segment failed");
}

static __thread cover_t *current_cover;

static intptr_t execute_syscall(const call_t *c, intptr_t a[kMaxArgs]) {
  if (c->call) {
    intptr_t temp =
        c->call(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    return temp;
  }
  intptr_t res = syscall(c->sys_nr, a[0], a[1], a[2], a[3], a[4], a[5]);
  // Some prctl commands don't respect the normal convention for return values
  // (e.g. PR_GET_TIMERSLACK, but there are more) and may produce all possible
  // errno values. This conflicts with fallback coverage.
  if (!flag_coverage && res == -1 && !strcmp(c->name, "prctl"))
    errno = EINVAL;
  return res;
}

static void cover_open(cover_t *cov, bool extra) {
  debug("234\n");
  int fd = open("/dev/cov", O_RDWR);
  debug("123\n");
  if (fd == -1) {
    FILE *tmpfs = fopen("/dev/cov", "a+");
    fclose(tmpfs);
  }
  
  fd = open("/dev/cov", O_RDWR); 
  if (dup2(fd, cov->fd) < 0)
    fail("filed to dup2(%d, %d) cover fd", fd, cov->fd);
  close(fd); 
  debug("234\n");
  cover_size = 640000; 
  if (system("/root/cov")) 
    fail("cover init trace write failed");
  debug("Rrooach executor_linux cov->fd = %d\n", cov->fd);
  mmap_alloc_size = cover_size * (is_kernel_64_bit ? 8 : 4);  
  // FILE *fp;
  // fp = fopen("/dev/cov", "w+");
  // fseek(fp, 0, SEEK_SET);
  // // int filesize = ftell(fp); 
  // // cov->data = (char*)malloc(filesize*sizeof(int));
  // // if(fread(cov->data,filesize+1, 1, fp))
  // fclose(fp);
  
  cov->data = (int *)mmap(NULL, (cover_size), PROT_READ | PROT_WRITE,
                           MAP_SHARED, cov->fd, 0);
  for(int i = 0; i < 1000; ++i)
    debug("%d\t", cov->data[i]); 
  if (cov->data == MAP_FAILED)
    fail("cover mmap failed");
  cov->data_end = cov->data + cover_size;
  debug("Rrooach: executor_linux 107 size = %d \n", cov->size ); 
}

static void cover_protect(cover_t *cov) {}

static void cover_unprotect(cover_t *cov) {}

template <typename kernel_u64_t>
static void enable_remote_cover(cover_t *cov, unsigned long ioctl_cmd,
                                unsigned int kcov_mode) {
  // kcov_remote_arg<kernel_u64_t, 1> arg = {
  //     .trace_mode = kcov_mode,
  // };
  // Coverage buffer size of background threads.
  // arg.area_size = kExtraCoverSize;
  // arg.num_handles = 1;
  // arg.handles[0].v = kcov_remote_handle(KCOV_SUBSYSTEM_USB, procid + 1);
  // arg.common_handle.v = kcov_remote_handle(KCOV_SUBSYSTEM_COMMON, procid +
  // 1);
  if (system("/root/cov"))
    exitf("remote cover enable write trace failed");
  // if (ioctl(cov->fd, ioctl_cmd, &arg))
  // 	exitf("remote cover enable write trace failed");
}

static void cover_enable(cover_t *cov, bool collect_comps, bool extra) {
  unsigned int kcov_mode = collect_comps ? KCOV_TRACE_CMP : KCOV_TRACE_PC;
  // The KCOV_ENABLE call should be fatal,
  // but in practice ioctl fails with assorted errors (9, 14, 25),
  // so we use exitf.
  if (!extra) {
    // if (ioctl(cov->fd, KCOV_ENABLE, kcov_mode))
    if (system("/root/cov"))
      exitf("cover enable write trace failed, mode=%d", kcov_mode);
    current_cover = cov;
    return;
  }
  if (is_kernel_64_bit)
    enable_remote_cover<uint64_aligned64>(cov, KCOV_REMOTE_ENABLE64, kcov_mode);
  else
    enable_remote_cover<uint64_aligned32>(cov, KCOV_REMOTE_ENABLE32, kcov_mode);
}

static void cover_reset(cover_t *cov) {
  // Callers in common_linux.h don't check this flag.
  if (!flag_coverage)
    return;
  if (cov == 0) {
    if (current_cover == 0)
      fail("cover_reset: current_cover == 0");
    cov = current_cover;
  }
  *(uint64 *)cov->data = 0;
}

static void cover_collect(cover_t *cov) {
  FILE *fp;
	int filesize;
	if ((fp=fopen("/dev/cov","r"))==NULL){
		debug("coverage file not exist\n"); 
	} 
	fseek(fp,0,SEEK_END);  
	filesize = ftell(fp); 
  cov->size = filesize/32;
  debug("Rrooach executor_linux175 size = %d\n", cov->size);
}

static bool cover_check(uint32 pc) { return true; }

// static bool cover_check(uint64 pc)
// {
// #if defined(__i386__) || defined(__x86_64__)
// 	// Text/modules range for x86_64.
// 	return pc >= 0xffffffff80000000ull && pc < 0xffffffffff000000ull;
// #else
// 	return true;
// #endif
// }

static bool detect_kernel_bitness() {
  if (sizeof(void *) == 8)
    return true;
  // It turns out to be surprisingly hard to understand if the kernel underneath
  // is 64-bits. A common method is to look at uname.machine. But it is produced
  // in some involved ways, and we will need to know about all strings it
  // returns and in the end it can be overriden during build and lie (and there
  // are known precedents of this). So instead we look at size of addresses in
  // /proc/kallsyms.
  bool wide = true;
  int fd = open("/proc/kallsyms", O_RDONLY);
  if (fd != -1) {
    char buf[16];
    if (read(fd, buf, sizeof(buf)) == sizeof(buf) &&
        (buf[8] == ' ' || buf[8] == '\t'))
      wide = false;
    close(fd);
  }
  debug("detected %d-bit kernel\n", wide ? 64 : 32);
  return wide;
}

// One does not simply exit.
// _exit can in fact fail.
// syzkaller did manage to generate a seccomp filter that prohibits exit_group
// syscall. Previously, we get into infinite recursion via segv_handler in such
// case and corrupted output_data, which does matter in our case since it is
// shared with fuzzer process. Loop infinitely instead. Parent will kill us. But
// one does not simply loop either. Compilers are sure that _exit never returns,
// so they remove all code after _exit as dead. Call _exit via volatile
// indirection. And this does not work as well. _exit has own handling of
// failing exit_group in the form of HLT instruction, it will divert control
// flow from our loop. So call the syscall directly.
NORETURN void doexit(int status) {
  volatile unsigned i;
  syscall(__NR_exit_group, status);
  for (i = 0;; i++) {
  }
}

#define SYZ_HAVE_FEATURES 1
static feature_t features[] = {
    {"leak", setup_leak},
    {"fault", setup_fault},
    {"binfmt_misc", setup_binfmt_misc},
    {"kcsan", setup_kcsan},
    {"usb", setup_usb},
};

static void setup_machine() {
  // nmi_check_duration() prints "INFO: NMI handler took too long" on slow debug
  // kernels. It happens a lot in qemu, and the messages are frequently
  // corrupted (intermixed with other kernel output as they are printed from
  // NMI) and are not matched against the suppression in pkg/report. This write
  // prevents these messages from being printed. Note: this is not executed in C
  // reproducers.
  if (!write_file("/sys/kernel/debug/x86/nmi_longest_ns", "10000000000"))
    printf("write to /sys/kernel/debug/x86/nmi_longest_ns failed: %s\n",
           strerror(errno));
}
