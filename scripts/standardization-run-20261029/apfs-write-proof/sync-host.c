
#include <assert.h>
#include <stddef.h>
#include <errno.h>
#define MNT_RDONLY 1
#define MNT_WAIT 1
#define VFS_RETURNED 0
#define NULLVP NULL
typedef void *vfs_context_t;
struct mount { unsigned flags; int sync_error,synced; };
typedef struct mount *mount_t;
struct node { mount_t v_mount; };
static struct mount mounts[3];
static struct node node;
static struct node *rootvnode;
static int iterate_calls,iteration_error;
static vfs_context_t vfs_context_current(void){return &node;}
static unsigned vfs_flags(mount_t m){return m->flags;}
static int VFS_SYNC(mount_t m,int wait,vfs_context_t c){assert(wait==MNT_WAIT&&c==&node);m->synced++;return m->sync_error;}
static int vfs_iterate(int flags,int(*cb)(mount_t,void*),void *arg){
 assert(!flags);iterate_calls++;
 for(unsigned i=0;i<3;i++)assert(cb(&mounts[i],arg)==VFS_RETURNED);
 return iteration_error;
}
struct ki_sync_state { vfs_context_t context; int error; };
static int
ki_sync_mount(mount_t mount, void *argument)
{
 struct ki_sync_state *state = argument;
 if (!(vfs_flags(mount) & MNT_RDONLY)) {
  int error = VFS_SYNC(mount, MNT_WAIT, state->context);
  if (!state->error) state->error = error;
 }
 return VFS_RETURNED;
}

static int
ki_vfs_sync(void)
{
 if (rootvnode == NULLVP || rootvnode->v_mount == NULL) return ENODEV;
 struct ki_sync_state state = { vfs_context_current(), 0 };
 int error = vfs_iterate(0, ki_sync_mount, &state);
 return state.error ? state.error : error;
}

int main(void){
 for(unsigned scenario=0;scenario<7;scenario++){
  for(unsigned i=0;i<3;i++)mounts[i]=(struct mount){.flags=i?0:1};
  node.v_mount=&mounts[0];rootvnode=&node;iterate_calls=iteration_error=0;
  if(scenario==1)rootvnode=NULL;
  if(scenario==2)node.v_mount=NULL;
  if(scenario==3){mounts[1].sync_error=EIO;mounts[2].sync_error=ENOSPC;}
  if(scenario==4)iteration_error=EBUSY;
  if(scenario==5)for(unsigned i=0;i<3;i++)mounts[i].flags=1;
  if(scenario==6)mounts[0].flags=0;
  int expected=(scenario==1||scenario==2)?ENODEV:scenario==3?EIO:scenario==4?EBUSY:0;
  assert(ki_vfs_sync()==expected);
  if(scenario==1||scenario==2)assert(!iterate_calls);
  else {
   assert(iterate_calls==1);
   for(unsigned i=0;i<3;i++)assert(mounts[i].synced==!(mounts[i].flags&MNT_RDONLY));
  }
 }
 return 0;
}
