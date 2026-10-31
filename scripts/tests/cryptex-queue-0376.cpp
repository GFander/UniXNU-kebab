#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdlib>
#define __unused
using UInt64=uint64_t;using UInt32=uint32_t;using IOReturn=int;using wait_result_t=int;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,kIOReturnNoDevice=3,kIOReturnNoResources=4,kIOReturnOverrun=5,kIOReturnIOError=6;
constexpr int THREAD_UNINT=0;constexpr unsigned KEBAB_BLOCK_CHUNK=65536;
struct IOLock{std::mutex m;std::condition_variable cv;};
static thread_local unsigned held;
static void IOLockLock(IOLock*l){l->m.lock();held++;}static void IOLockUnlock(IOLock*l){held--;l->m.unlock();}
static void IOLockWakeup(IOLock*l,void*,bool){l->cv.notify_all();}
static int IOLockSleep(IOLock*l,void*,int){std::unique_lock<std::mutex>g(l->m,std::adopt_lock);held--;l->cv.wait(g);held++;g.release();return 0;}
static std::thread::id current_thread(){return std::this_thread::get_id();}struct WorkerDone{};static void thread_terminate(std::thread::id){throw WorkerDone{};}
static std::atomic<unsigned> allocations,frees;static bool failAllocation;
static void*IOMalloc(size_t n){if(failAllocation)return nullptr;allocations++;return malloc(n);}static void IOFree(void*p,size_t){frees++;free(p);}
static void IOLog(const char*,...){}
struct IOService{};struct IOBlockStorageDevice{static void stop(IOService*){}};
struct IOMemoryDescriptor{std::vector<uint8_t> bytes;unsigned refs=1;bool shortWrite=false;IOMemoryDescriptor(size_t n):bytes(n){}void retain(){refs++;}void release(){refs--;}size_t writeBytes(UInt64 off,const void*p,unsigned n){if(shortWrite)return 0;assert(off+n<=bytes.size());memcpy(bytes.data()+off,p,n);return n;}};
struct IOStorageCompletion{void*target;void*parameter;void(*action)(void*,void*,IOReturn,UInt64);};
static std::mutex backendLock;static std::condition_variable backendCV;static bool pauseBackend,enteredBackend;static UInt64 failOffset=~0ULL;
static int kebab_cryptex_media_read(UInt64 off,void*p,unsigned n){assert(!held);std::unique_lock<std::mutex>g(backendLock);enteredBackend=true;backendCV.notify_all();backendCV.wait(g,[]{return !pauseBackend;});if(off==failOffset)return 5;for(unsigned i=0;i<n;i++)((uint8_t*)p)[i]=(uint8_t)((off+i)*17);return 0;}
class KebabBlockStorageDevice:public IOBlockStorageDevice{public:
 struct FileRequest{FileRequest*next;IOMemoryDescriptor*buffer;UInt64 offset,length;IOStorageCompletion completion;};
 bool fOnline=true,fFileStopping=false,fFileWorkerActive=true,fFileStorage=true,fMediaStateReported=false;
 std::thread::id fFileThread;IOLock lock;IOLock*fLock=&lock;unsigned fFilePending=0,fFileHighWater=0;UInt64 fFileAccepted=0,fFileCompleted=0,fFileFailed=0;FileRequest*fFileHead=nullptr,*fFileTail=nullptr;uint8_t bounce[65536];void*fBounce=bounce;std::atomic<unsigned> refs{2};
 void retain(){refs++;}void release(){refs--;}static IOReturn statusForError(int e){return e?kIOReturnIOError:0;}
 IOReturn enqueueFileRead(IOMemoryDescriptor*,UInt64,UInt64,IOStorageCompletion*);static void fileWorker(void*,wait_result_t);void stop(IOService*);
};
IOReturn
KebabBlockStorageDevice::enqueueFileRead(IOMemoryDescriptor *buffer, UInt64 offset,
    UInt64 length, IOStorageCompletion *completion)
{
	if (!completion || !completion->action) return kIOReturnBadArgument;
	FileRequest *request = (FileRequest *)IOMalloc(sizeof(*request));
	if (!request) return kIOReturnNoMemory;
	request->next = NULL; request->buffer = buffer;
	request->offset = offset; request->length = length; request->completion = *completion;
	IOLockLock(fLock);
	IOReturn status = !fOnline || fFileStopping ? kIOReturnNoDevice :
	    fFilePending == ~0U ? kIOReturnNoResources : kIOReturnSuccess;
 bool crossedOldLimit=false;
	if (status == kIOReturnSuccess) {
		buffer->retain(); fFilePending++;fFileAccepted++;
  if(fFilePending>fFileHighWater){fFileHighWater=fFilePending;crossedOldLimit=fFileHighWater==33;}
		if (fFileTail) fFileTail->next = request; else fFileHead = request;
		fFileTail = request;
		IOLockWakeup(fLock, &fFileHead, false);
	}
	IOLockUnlock(fLock);
	if (status != kIOReturnSuccess) IOFree(request, sizeof(*request));
 if(crossedOldLimit)IOLog("KEBAB CRYPTEX QUEUE: pending crossed old32 limit; request accepted, no Busy retry assumption\n");
	return status;
}
void
KebabBlockStorageDevice::fileWorker(void *argument, wait_result_t result __unused)
{
	KebabBlockStorageDevice *self = (KebabBlockStorageDevice *)argument;
	for (;;) {
		IOLockLock(self->fLock);
		while (!self->fFileHead && !self->fFileStopping)
			(void)IOLockSleep(self->fLock, &self->fFileHead, THREAD_UNINT);
		FileRequest *request = self->fFileHead;
		if (!request) { IOLockUnlock(self->fLock); break; }
		self->fFileHead = request->next;
		if (!self->fFileHead) self->fFileTail = NULL;
		bool online = self->fOnline && !self->fFileStopping;
		IOLockUnlock(self->fLock);
		IOReturn status = online ? kIOReturnSuccess : kIOReturnNoDevice;
		UInt64 done = 0;
		while (status == kIOReturnSuccess && done < request->length) {
			UInt64 left = request->length - done;
			UInt32 count = (UInt32)(left < KEBAB_BLOCK_CHUNK ? left : KEBAB_BLOCK_CHUNK);
			status = statusForError(kebab_cryptex_media_read(request->offset + done, self->fBounce, count));
			if (status != kIOReturnSuccess) break;
			if (request->buffer->writeBytes(done, self->fBounce, count) != count) {
				status = kIOReturnOverrun; break;
			}
			done += count;
		}
		IOLockLock(self->fLock);
		self->fFilePending--;
		IOLockUnlock(self->fLock);
		/* Complete exactly once without either bridge or backing FS locks. */
		request->completion.action(request->completion.target, request->completion.parameter, status, done);
		request->buffer->release(); IOFree(request, sizeof(*request));
  IOLockLock(self->fLock);
  UInt64 completed=++self->fFileCompleted;
  if(status!=kIOReturnSuccess)self->fFileFailed++;
  UInt64 accepted=self->fFileAccepted,failed=self->fFileFailed;
  unsigned pending=self->fFilePending,high=self->fFileHighWater;
  IOLockUnlock(self->fLock);
  if((completed>=128 && !(completed&(completed-1))) || (status!=kIOReturnSuccess && failed<=8))
   IOLog("KEBAB CRYPTEX QUEUE: accepted=%llu completed=%llu failed=%llu pending=%u high=%u last=0x%x bytes=%llu\n",(unsigned long long)accepted,(unsigned long long)completed,(unsigned long long)failed,pending,high,status,(unsigned long long)done);
	}
	IOLockLock(self->fLock);
	self->fFileWorkerActive = false;
	IOLockWakeup(self->fLock, &self->fFileWorkerActive, false);
	IOLockUnlock(self->fLock);
	self->release();
	thread_terminate(current_thread());
}
void
KebabBlockStorageDevice::stop(IOService *provider)
{
	/* Wait for the current chunk loop; reject later callers before I/O. */
	IOLockLock(fLock);
	fOnline = false;
	fMediaStateReported = false;
	if (fFileStorage) {
		fFileStopping = true;
		IOLockWakeup(fLock, &fFileHead, false);
		/* Normal stop drains accepted requests. A completion may reenter stop
		 * on this worker; its retain defers free until the remaining queue drains. */
		while (fFileWorkerActive && current_thread() != fFileThread)
			(void)IOLockSleep(fLock, &fFileWorkerActive, THREAD_UNINT);
	}
	IOLockUnlock(fLock);
	IOBlockStorageDevice::stop(provider);
}

struct Completion{unsigned count=0;int status=-1;UInt64 bytes=0;KebabBlockStorageDevice*stop=nullptr;};
static void completed(void*,void*p,int e,UInt64 n){assert(!held);auto*c=(Completion*)p;assert(!c->count);c->count++;c->status=e;c->bytes=n;if(c->stop)c->stop->stop(nullptr);}
static std::thread worker(KebabBlockStorageDevice&d){return std::thread([&]{d.fFileThread=current_thread();try{d.fileWorker(&d,0);}catch(WorkerDone&){};});}
int main(){unsigned cases=0;
 // Burst larger than the old cap, before any worker runs. Actual production
 // queue must accept and complete all256, preserve bytes and balance refs.
 {KebabBlockStorageDevice d;std::vector<IOMemoryDescriptor*>buffers;std::vector<Completion>done(256);
 for(unsigned i=0;i<256;i++){auto*b=new IOMemoryDescriptor(4096);buffers.push_back(b);IOStorageCompletion c{nullptr,&done[i],completed};assert(!d.enqueueFileRead(b,i*4096,4096,&c));cases++;}
 assert(d.fFilePending==256&&d.fFileHighWater==256&&d.fFileAccepted==256);auto t=worker(d);
 // Wait without racing non-atomic counters; use the actual queue mutex.
 for(;;){IOLockLock(d.fLock);bool ready=d.fFileCompleted==256;IOLockUnlock(d.fLock);if(ready)break;std::this_thread::yield();}
 d.stop(nullptr);t.join();assert(!d.fFilePending&&d.fFileCompleted==256&&!d.fFileFailed&&d.refs==1);
 for(unsigned i=0;i<256;i++){assert(done[i].count==1&&done[i].status==0&&done[i].bytes==4096&&buffers[i]->refs==1);for(unsigned j=0;j<4096;j++)assert(buffers[i]->bytes[j]==(uint8_t)((i*4096+j)*17));delete buffers[i];}cases++;}
 // Invalid completion, true allocation failure and offline admission never
 // retain a buffer or call completion. Counter overflow is not a Busy retry.
 {KebabBlockStorageDevice d;IOMemoryDescriptor b(4096);Completion done;IOStorageCompletion c{nullptr,&done,completed};assert(d.enqueueFileRead(&b,0,4096,nullptr)==kIOReturnBadArgument);cases++;failAllocation=true;assert(d.enqueueFileRead(&b,0,4096,&c)==kIOReturnNoMemory);failAllocation=false;cases++;d.fOnline=false;assert(d.enqueueFileRead(&b,0,4096,&c)==kIOReturnNoDevice);cases++;d.fOnline=true;d.fFilePending=~0U;assert(d.enqueueFileRead(&b,0,4096,&c)==kIOReturnNoResources&&!done.count&&b.refs==1);cases++;}
 // Completion-triggered stop cannot deadlock. Accepted tail requests drain
 // exactly once with NoDevice, retaining the original stop policy.
 {KebabBlockStorageDevice d;IOMemoryDescriptor a(4096),b(4096);Completion x,y;x.stop=&d;IOStorageCompletion cx{nullptr,&x,completed},cy{nullptr,&y,completed};assert(!d.enqueueFileRead(&a,0,4096,&cx)&&!d.enqueueFileRead(&b,4096,4096,&cy));auto t=worker(d);t.join();assert(x.count==1&&x.status==0&&y.count==1&&y.status==kIOReturnNoDevice&&d.fFileCompleted==2&&d.fFileFailed==1&&!d.fFilePending&&!d.fFileWorkerActive&&a.refs==1&&b.refs==1);cases++;}
 // Real backend failure is propagated after a successful first chunk, never
 // converted into success. Completion overrun is propagated separately.
 for(unsigned which=0;which<2;which++){KebabBlockStorageDevice d;IOMemoryDescriptor a(131072);a.shortWrite=which==1;Completion x;x.stop=&d;IOStorageCompletion c{nullptr,&x,completed};failOffset=which==0?65536:~0ULL;assert(!d.enqueueFileRead(&a,0,131072,&c));auto t=worker(d);t.join();assert(x.count==1&&x.status==(which==0?kIOReturnIOError:kIOReturnOverrun)&&x.bytes==(which==0?65536u:0u)&&d.fFileFailed==1&&a.refs==1);cases++;}failOffset=~0ULL;
 assert(allocations==frees);printf("PASS %u actual queue/worker/stop burst, callback reentry, backend failure and ownership cases; IOStorage descriptors/backing reads mocked, real host mutex/CV threads; no hardware proof\n",cases);
}
