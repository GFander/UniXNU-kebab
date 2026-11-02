#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <openssl/sha.h>
#define EAUTH 80
#define KEBAB_OFFLINE_CORE_ONLY 1
#include "kebab_offline_setup_impl.h"
struct file {bool exists;uint64_t inode;uint32_t uid,gid,mode,flags,protection;int64_t times[8];uint8_t acl[32],data[128];unsigned acl_bytes,size;uint32_t metadata_flags;uint8_t owner_uuid[16],group_uuid[16];uint64_t nlink;int64_t backup_time[2];};
static struct file fs[4][3];static int calls,fail_at,stages,moves,partial;
static unsigned checks;
static void hash(const void*p,size_t n,uint8_t out[32]){SHA256(p,n,out);}
static unsigned slot(unsigned i){switch(i){case 0:return 0;case 8:return 1;case 13:return 2;case 14:return 3;default:assert(0);return 0;}}
static int fault(void){calls++;return calls==fail_at?EIO:0;}
static int record(unsigned index,unsigned kind,struct kebab_offline_record*r){int e=fault();if(e)return e;assert(kind<3);struct file*f=&fs[slot(index)][kind];memset(r,0,sizeof(*r));r->exists=f->exists;if(!f->exists)return 0;r->fileid=f->inode;r->size=f->size;r->uid=f->uid;r->gid=f->gid;r->mode=f->mode;r->flags=f->flags;r->protection=f->protection;r->acl_bytes=f->acl_bytes;r->metadata_flags=f->metadata_flags;r->nlink=f->nlink;memcpy(r->owner_uuid,f->owner_uuid,16);memcpy(r->group_uuid,f->group_uuid,16);memcpy(r->backup_time,f->backup_time,sizeof(r->backup_time));memcpy(r->times,f->times,sizeof(r->times));hash(f->data,f->size,r->hash);hash(f->acl,f->acl_bytes,r->acl_hash);return 0;}
static int stage(const struct kebab_offline_entry*e,const uint8_t*p){int error=fault();if(error)return error;struct file*f=&fs[slot(e->index)][2];if(f->exists)return EEXIST;assert(e->new_size<=128);memset(f,0,sizeof(*f));f->exists=true;f->inode=1000+slot(e->index);f->uid=e->uid;f->gid=e->gid;f->mode=e->mode;f->flags=e->flags;f->protection=e->protection;f->metadata_flags=e->metadata_flags;f->nlink=1;memcpy(f->owner_uuid,e->owner_uuid,16);memcpy(f->group_uuid,e->group_uuid,16);memcpy(f->backup_time,e->backup_time,sizeof(f->backup_time));f->acl_bytes=fs[slot(e->index)][0].acl_bytes;memcpy(f->acl,fs[slot(e->index)][0].acl,f->acl_bytes);memcpy(f->times,e->times,sizeof(f->times));memcpy(f->data,p,e->new_size);f->size=e->new_size;stages++;if(partial){f->size--;return EIO;}return 0;}
static int move(unsigned index,unsigned operation){int error=fault();if(error)return error;unsigned i=slot(index);struct file temp;assert(operation<5);moves++;
 if(operation==0||operation==2){unsigned other=operation==0?2:1;if(!fs[i][0].exists||!fs[i][other].exists)return ENOENT;temp=fs[i][0];fs[i][0]=fs[i][other];fs[i][other]=temp;fs[i][0].times[6]++;fs[i][other].times[6]++;return 0;}
 unsigned from=operation==1?2:operation==3?0:1,to=operation==4?0:1;if(fs[i][to].exists)return EEXIST;if(!fs[i][from].exists)return ENOENT;fs[i][to]=fs[i][from];memset(&fs[i][from],0,sizeof(struct file));fs[i][to].times[6]++;return 0;}
static int sync_parent(unsigned index){(void)slot(index);return fault();}
static const struct kebab_offline_io io={record,stage,move,sync_parent};
static uint8_t packet[4096];static uint32_t packet_size;static struct kebab_offline_entry original[4];static unsigned order[]={13,14,0,8};
static void init(void){memset(fs,0,sizeof(fs));calls=stages=moves=partial=0;fail_at=-1;uint32_t head[4]={KEBAB_OFFLINE_MAGIC,1,1,4};memcpy(packet,head,16);packet_size=16;
 for(unsigned j=0;j<4;j++){unsigned index=order[j],i=slot(index);struct file*f=&fs[i][0];f->exists=true;f->inode=100+i;f->uid=index==13?0:501;f->gid=index==8?501:index==14?0xfffffffeu:0;f->mode=index==8?0100600:index==14?0100666:0100644;f->protection=4;f->nlink=1;f->metadata_flags=index==14?7:4;for(unsigned k=0;k<16&&index==14;k++){f->owner_uuid[k]=17+k;f->group_uuid[k]=128+k;}for(unsigned t=0;t<8;t++)f->times[t]=t%2?7:1793000000+t;snprintf((char*)f->data,128,"original-%u",index);f->size=strlen((char*)f->data);if(index==14){f->acl_bytes=32;for(unsigned k=0;k<32;k++)f->acl[k]=k+1;}
 struct kebab_offline_entry e={0};e.index=index;e.new_size=index==0?0:f->size;e.fileid=f->inode;e.old_size=f->size;e.uid=f->uid;e.gid=f->gid;e.mode=f->mode;e.protection=4;e.acl_bytes=f->acl_bytes;e.metadata_flags=f->metadata_flags;e.nlink=f->nlink;memcpy(e.owner_uuid,f->owner_uuid,16);memcpy(e.group_uuid,f->group_uuid,16);memcpy(e.backup_time,f->backup_time,sizeof(e.backup_time));memcpy(e.times,f->times,sizeof(e.times));hash(f->data,f->size,e.old_hash);hash(f->acl,f->acl_bytes,e.acl_hash);uint8_t next[128];memcpy(next,f->data,f->size);if(index==8)next[0]='N';if(e.new_size)hash(next,e.new_size,e.new_hash);original[j]=e;memcpy(packet+packet_size,&e,sizeof(e));packet_size+=sizeof(e);if(e.new_size){memcpy(packet+packet_size,next,e.new_size);packet_size+=e.new_size;}}
}
static void old_files_retained(void){for(unsigned j=0;j<4;j++){unsigned i=slot(original[j].index),found=0;for(unsigned kind=0;kind<3;kind++){struct file*f=&fs[i][kind];if(f->exists&&f->inode==original[j].fileid){uint8_t digest[32],acl[32];hash(f->data,f->size,digest);hash(f->acl,f->acl_bytes,acl);assert(f->size==original[j].old_size&&!memcmp(digest,original[j].old_hash,32));assert(f->uid==original[j].uid&&f->gid==original[j].gid&&f->mode==original[j].mode&&f->protection==4&&f->nlink==1&&!memcmp(f->owner_uuid,original[j].owner_uuid,16)&&!memcmp(f->group_uuid,original[j].group_uuid,16)&&!memcmp(acl,original[j].acl_hash,32));found++;}}assert(found==1);}checks++;}
static int run(void){return kebab_offline_plan_run(packet,packet_size,&io,hash);}
int main(void){init();assert(!run());int total=calls;old_files_retained();assert(!fs[0][0].exists&&fs[0][1].exists);assert(stages==3&&moves==7);int old_stages=stages,old_moves=moves;assert(!run()&&old_stages==stages&&old_moves==moves);checks++;
 for(int point=1;point<=total;point++){init();fail_at=point;assert(run()!=0);old_files_retained();fail_at=-1;
 struct file saved_fs[4][3];memcpy(saved_fs,fs,sizeof(fs));uint8_t saved_packet[4096];memcpy(saved_packet,packet,packet_size);uint32_t saved_size=packet_size;
 uint32_t reverse_head[4]={KEBAB_OFFLINE_MAGIC,1,2,0};packet_size=16;
 for(unsigned j=0;j<4;j++){struct kebab_offline_entry e=original[j];unsigned i=slot(e.index);bool retained_elsewhere=(fs[i][1].exists&&fs[i][1].inode==e.fileid)||(fs[i][2].exists&&fs[i][2].inode==e.fileid);if(!retained_elsewhere)continue;struct file*f=&fs[i][0];e.new_size=f->exists?f->size:0;e.current_fileid=f->exists?f->inode:0;if(f->exists)hash(f->data,f->size,e.new_hash);else memset(e.new_hash,0,32);memcpy(packet+packet_size,&e,sizeof(e));packet_size+=sizeof(e);reverse_head[3]++;}
 if(reverse_head[3]){memcpy(packet,reverse_head,16);assert(!run());old_files_retained();for(unsigned j=0;j<4;j++)assert(fs[slot(original[j].index)][0].inode==original[j].fileid);checks++;}
 memcpy(fs,saved_fs,sizeof(fs));memcpy(packet,saved_packet,saved_size);packet_size=saved_size;
 assert(!run());old_files_retained();checks++;}
 init();partial=1;assert(run()==EIO);old_files_retained();partial=0;assert(run()==EEXIST);old_files_retained();checks++;
 for(unsigned mode=0;mode<18;mode++){init();struct kebab_offline_entry e;memcpy(&e,packet+16,sizeof(e));switch(mode){case 0:packet[0]^=1;break;case 1:((uint32_t*)packet)[3]=5;break;case 2:e.index=99;break;case 3:e.fileid++;break;case 4:e.acl_hash[0]^=1;break;case 5:e.protection=3;break;case 6:e.new_hash[0]^=1;break;case 7:e.times[1]=1000000000;break;case 8:e.uid=501;break;case 9:e.old_size=65537;break;case 10:fs[1][0].inode++;break;case 11:fs[1][0].data[0]='X';break;case 12:fs[1][0].protection=3;break;case 13:fs[3][0].acl[0]^=1;break;case 14:fs[3][0].group_uuid[0]^=1;break;case 15:fs[1][0].nlink=2;break;case 16:e.metadata_flags=8;break;case 17:e.backup_time[1]=1000000000;break;}
 if((mode>=2&&mode<=9)||mode==16||mode==17)memcpy(packet+16,&e,sizeof(e));assert(run()!=0&&stages==0&&moves==0);checks++;}
 /* Roll back all originals after an original MG rebuild. Native DataArk
  * checkpoints contain identical bytes but a different file key/inode. */
 init();assert(!run());fs[0][0]=fs[0][1];fs[0][0].inode=9000;memcpy(fs[0][0].data,"new-cache",9);fs[0][0].size=9;
 uint32_t head[4]={KEBAB_OFFLINE_MAGIC,1,2,4};memcpy(packet,head,16);packet_size=16;
 for(unsigned j=0;j<4;j++){struct kebab_offline_entry e=original[j];struct file*f=&fs[slot(e.index)][0];e.new_size=f->size;e.current_fileid=f->inode;hash(f->data,f->size,e.new_hash);memcpy(packet+packet_size,&e,sizeof(e));packet_size+=sizeof(e);}
 assert(!run());old_files_retained();for(unsigned j=0;j<4;j++)assert(fs[slot(original[j].index)][0].inode==original[j].fileid);checks++;
 printf("PASS transaction core: %u checks, %d injected callback interruption points; original ACL/class/contents/inodes retained\\n",checks,total);return 0;}
