/* ===================================================================
 * Copyright (c) 2005-2007 Vadim Druzhin (cdslow@mail.ru).
 * All rights reserved.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 * ===================================================================
 */

/*
 * This file uses parts of code from chntpw.c, which is
 * Copyright (c) 1997-2007 Petter Nordahl-Hagen.
 */ 

#include <stdlib.h>
#include <stdio.h>
#include <mem.h>
#include <ctype.h>
#include <openssl/des.h>
#include <openssl/md4.h>
#include "ntreg.h"
#include "sam.h"
#include "log.h"
#include "ntpw.h"

#define MD4Init MD4_Init
#define MD4Update MD4_Update
#define MD4Final MD4_Final

static struct hive *hive[H_COUNT];
static int max_sam_lock = 0;
static char user_path_1[]="\\SAM\\Domains\\Account\\Users\\";
static char user_path_V[]="\\V";
static char user_path_F[]="\\F";

static int compute_pw(char *buf, int rid, int vlen, char *password);
static void check_get_samdata(void);
static void sid_to_key1(unsigned long sid,unsigned char deskey[8]);
static void sid_to_key2(unsigned long sid,unsigned char deskey[8]);
static void E1(unsigned char *k, unsigned char *d, unsigned char *out);
static void make_lanmpw(char *p, unsigned char *lm, int len);
static struct keyval *get_user_f(int rid);

int open_hives(char *fname[H_COUNT])
    {
    enum HIVE_ID i;
    int count;

    count=0;
    for(i=H_SAM; i<H_COUNT; ++i)
        {
        if(NULL!=hive[i])
            closeHive(hive[i]);
        hive[i]=openHive(fname[i], HMODE_RW/*|HMODE_NOALLOC|HMODE_VERBOSE*/);
        if(NULL==hive[i])
            WriteLog("open_hives: Unable to open/read a hive '%s'\n",
                fname[i]);
        else
            {
            ++count;
            if(hive[i]->state & HMODE_RO)
                WriteLog("open_hives: Hive '%s' opened readonly!\n",
                    fname[i]);
            }
        }

    check_get_samdata();

    return count;
    }

/* Check if hive is SAM, and if it is, extract some
 * global policy information from it, like lockout counts etc
 */
static void check_get_samdata(void)
    {
    struct keyval *v;
    struct accountdb_F *f;

    if(NULL==hive[H_SAM])
        return;

    /* Get users F value */
    v=get_val2buf(hive[H_SAM], NULL, 0, ACCOUNTDB_F_PATH, REG_BINARY);
    if(!v)
        {
        WriteLog("check_get_samdata: Login counts data not found in SAM!\n");
        return;
        }

    f=(struct accountdb_F *)&v->data;
    max_sam_lock=f->locklimit;
    }

void close_hives(void)
    {
    enum HIVE_ID i;

    for(i=H_SAM; i<H_COUNT; ++i)
        {
        if(NULL!=hive[i])
            {
            closeHive(hive[i]);
            hive[i]=NULL;
            }
        }
    }

int is_hives_dirty(void)
    {
    enum HIVE_ID i;

    for(i=H_SAM; i<=H_COUNT; ++i)
        {
        if(NULL==hive[i])
            continue;
	if(hive[i]->state & HMODE_DIRTY)
            return 1;
	}

    return 0;
    }

int is_hives_ro(void)
    {
    enum HIVE_ID i;

    for(i=H_SAM; i<=H_COUNT; ++i)
        {
        if(NULL==hive[i])
            continue;
	if(hive[i]->state & HMODE_RO)
            return 1;
	}

    return 0;
    }

int write_hives(void)
    {
    enum HIVE_ID i;
    int ret=1;

    for(i=H_SAM; i<=H_COUNT; ++i)
        {
        if(NULL==hive[i])
            continue;
	if(hive[i]->state & HMODE_DIRTY)
            {
            WriteLog("Writing hive <%s> ", hive[i]->filename);
	    if(0==writeHive(hive[i]))
	        WriteLog("OK\n");
            else
                {
	        WriteLog("FAILED!\n");
                ret=0;
                }
	    }
	}

    return ret;
    }

struct user_info *first_user(struct search_user *su)
    {
    if(NULL==hive[H_SAM])
        return NULL;

    su->count=0;
    su->countri=0;
    su->nkofs=trav_path(hive[H_SAM], 0,"\\SAM\\Domains\\Account\\Users\\Names\\",0);

    return next_user(su);
    }

struct user_info *next_user(struct search_user *su)
    {
    struct ex_data ex;
    int name_size;
    struct user_info *ui;

    if(!su->nkofs)
        {
        WriteLog("next_user: Cannot find usernames in registry! (is this a SAM-hive?)\n");
        return NULL;
        }

    ex.name=NULL;
    if(ex_next_n(hive[H_SAM], su->nkofs+4, &su->count, &su->countri, &ex)<=0)
        return NULL;
    if(NULL!=ex.name)
        free(ex.name);

    name_size=ex.nk->len_name;
    if(0x20==ex.nk->type)
        name_size*=2; /* ASCII key name */
    name_size+=2;

    ui=malloc(sizeof(*ui)+name_size);
    if(NULL==ui)
        {
        WriteLog("next_user: Memory full!\n");
        return NULL;
        }
    ui->unicode_name=(char *)(ui+1);
    
    if(0x20==ex.nk->type)
        /* ASCII key name */
        cheap_ascii2uni(ex.nk->keyname, ui->unicode_name, ex.nk->len_name);
    else
        /* UNICODE key name */
        memcpy(ui->unicode_name, ex.nk->keyname, ex.nk->len_name);
    ui->unicode_name[name_size-2]=0;
    ui->unicode_name[name_size-1]=0;

    /* Extract the value out of the username-key, value is RID  */
    ui->rid = get_dword(hive[H_SAM], ex.nkoffs+4, "@");

    return ui;
    }

int is_account_locked(int rid)
    {
    struct user_F *f;
    struct keyval *v;
    unsigned short acb;
    int ret;

    /* Get users F value */
    v=get_user_f(rid);
    if(NULL==v)
        return -1;

    f = (struct user_F *)&v->data;
    acb = f->ACB_bits;

    WriteLog("Failed login count: %u, while max tries is: %u\n",
        f->failedcnt, max_sam_lock);
    WriteLog("Total  login count: %u\n", f->logins);
    
    if(acb&(ACB_DISABLED|ACB_AUTOLOCK) ||
        (f->failedcnt > 0 && f->failedcnt >= max_sam_lock))
        {
	WriteLog("Account is %s\n",
            (acb & ACB_DISABLED) ? "disabled" : "probably locked out!");
        ret=1;
        }
    else
        ret=0;

    free(v);

    return ret;
    }

/* Try to decode and possibly change account lockout etc
 * This is \SAM\Domains\Account\Users\<RID>\F
 * It's size seems to always be 0x50.
 * Params: RID - user ID, mode - 0 silent, 1 silent, 2 edit.
 * Returns: ACB bits with high bit set if lockout count is >0
 */
int unlock_account(int rid)
    {
    char s[sizeof(user_path_1)+8+sizeof(user_path_F)];
    struct user_F *f;
    struct keyval *v;
    unsigned short acb;

    /* Get users F value */
    v=get_user_f(rid);
    if(NULL==v)
        return 0;

    f = (struct user_F *)&v->data;
    acb = f->ACB_bits;

    acb |= ACB_PWNOEXP;
    acb &= ~ACB_DISABLED;
    acb &= ~ACB_AUTOLOCK;
    f->ACB_bits = acb;
    f->failedcnt = 0;
    sprintf(s, "%s%08X%s", user_path_1, rid&0xFFFFFFFFu, user_path_F);
    put_buf2val(hive[H_SAM], v, 0, s, REG_BINARY);
    WriteLog("Unlocked!\n");

    free(v);

    return 1;
    }

static struct keyval *get_user_f(int rid)
    {
    char s[sizeof(user_path_1)+8+sizeof(user_path_F)];
    struct keyval *v;

    /* Get users F value */
    sprintf(s, "%s%08X%s", user_path_1, rid&0xFFFFFFFFu, user_path_F);
    v = get_val2buf(hive[H_SAM], NULL, 0, s, REG_BINARY);
    if(!v)
        {
        WriteLog("get_user_f: Cannot find value <%s>!\n", s);
        return(NULL);
        }

    if(v->len!=0x50)
        {
        WriteLog("get_user_f: F value is 0x%x bytes, not 0x50,"
            " unable to check account flags!\n", v->len);
        free(v);
        return(NULL);
        }

    return v;
    }

int change_password(int rid, char *password)
    {
    struct keyval *v;
    char s[sizeof(user_path_1)+8+sizeof(user_path_V)];
    int ret;

    sprintf(s, "%s%08X%s", user_path_1, rid&0xFFFFFFFFu, user_path_V);
    v = get_val2buf(hive[H_SAM], NULL, 0, s, REG_BINARY);
    if(!v)
        {
        WriteLog("change_password: Cannot find value <%s>\n", s);
        return 0;
        }

    if(v->len<0xcc)
        {
        WriteLog("change_password: Value <%s> is too short (only %ld bytes)"
            " to be a SAM user V-struct!\n", s, v->len);
        free(v);
        return 0;
        }

    ret=compute_pw((char *)&v->data , rid, v->len, password);
    if(ret)
        {
        if(!(put_buf2val(hive[H_SAM], v, 0, s, REG_BINARY)))
            {
	     WriteLog("change_password: Failed to write updated <%s> to registry!"
                " Password change not completed!\n", s);
            ret=0;
            }
        }

    free(v);

    return ret;
    }

/* Decode the V-struct, and change the password
 * vofs - offset into SAM buffer, start of V struct
 * rid - the users RID, required for the DES decrypt stage
 *
 * Some of this is ripped & modified from pwdump by Jeremy Allison
 * 
 */
static int compute_pw(char *buf, int rid, int vlen, char *password)
    {
    unsigned char x1[] = {0x4B,0x47,0x53,0x21,0x40,0x23,0x24,0x25};
    int pl;
    char *vp;
    static char username[128],fullname[128];
    char comment[128],homedir[128],md4[32];
    unsigned char lanman[32];
    char newunipw[34], newp[20], despw[20];
    unsigned char newlanpw[16];
    char newlandes[20];
    int username_offset,username_len;
    int fullname_offset,fullname_len;
    int comment_offset,comment_len;
    int homedir_offset,homedir_len;
    int ntpw_len,lmpw_len,ntpw_offs,lmpw_offs,i;
    int all_pw_blank = 0;
    struct user_V *v;

    des_key_schedule ks1, ks2;
    des_cblock deskey1, deskey2;

    MD4_CTX context;
    unsigned char digest[16];

    v = (struct user_V *)buf;
    vp = buf;
 
    username_offset = v->username_ofs;
    username_len    = v->username_len; 
    fullname_offset = v->fullname_ofs;
    fullname_len    = v->fullname_len;
    comment_offset  = v->comment_ofs;
    comment_len     = v->comment_len;
    homedir_offset  = v->homedir_ofs;
    homedir_len     = v->homedir_len;
    lmpw_offs       = v->lmpw_ofs;
    lmpw_len        = v->lmpw_len;
    ntpw_offs       = v->ntpw_ofs;
    ntpw_len        = v->ntpw_len;

    *username = 0;
    *fullname = 0;
    *comment = 0;
    *homedir = 0;
   
    if(username_len <= 0 || username_len > vlen ||
        username_offset <= 0 || username_offset >= vlen ||
        comment_len < 0 || comment_len > vlen   ||
        fullname_len < 0 || fullname_len > vlen ||
        homedir_offset < 0 || homedir_offset >= vlen ||
        comment_offset < 0 || comment_offset >= vlen ||
        lmpw_offs < 0 || lmpw_offs >= vlen)
        {
        WriteLog("compute_pw: Not a legal struct?"
            " (negative struct lengths)\n");
        return(0);
        }

    /* Offsets in top of struct is relative to end of pointers, adjust */
    username_offset += 0xCC;
    fullname_offset += 0xCC;
    comment_offset += 0xCC;
    homedir_offset += 0xCC;
    ntpw_offs += 0xCC;
    lmpw_offs += 0xCC;
   
    cheap_uni2ascii(vp + username_offset,username,username_len);
    cheap_uni2ascii(vp + fullname_offset,fullname,fullname_len);
    cheap_uni2ascii(vp + comment_offset,comment,comment_len);
    cheap_uni2ascii(vp + homedir_offset,homedir,homedir_len);
   
    WriteLog("RID     : %04d [%04x]\n",rid,rid);
    WriteLog("Username: %s\n",username);
    WriteLog("fullname: %s\n",fullname);
    WriteLog("comment : %s\n",comment);
    WriteLog("homedir : %s\n\n",homedir);
   
    if(lmpw_len<16)
        WriteLog("** LANMAN password not set. User MAY have a blank"
            " password.\n** Usually safe to continue\n");

    if(ntpw_len<16)
        {
        WriteLog("** No NT MD4 hash found. This user probably has a"
            " BLANK password!\n");
        if(lmpw_len<16)
            {
	    WriteLog("** No LANMAN hash found either. Sorry, cannot change."
                " Try login with no password!\n");
	    all_pw_blank = 1;
            }
        else
            {
	    WriteLog("** LANMAN password IS however set."
                " Will now install new password as NT pass instead.\n");
	    WriteLog("** NOTE: Continue at own risk!\n");
	    ntpw_offs = lmpw_offs;
	    *(unsigned int *)(vp+0xa8) = ntpw_offs - 0xcc;
	    ntpw_len = 16;
	    lmpw_len = 0;
            }
        }

    /* Get the two decrpt keys. */
    sid_to_key1(rid,(unsigned char *)deskey1);
    des_set_key((des_cblock *)deskey1,ks1);
    sid_to_key2(rid,(unsigned char *)deskey2);
    des_set_key((des_cblock *)deskey2,ks2);
   
    /* Decrypt the NT md4 password hash as two 8 byte blocks. */
    des_ecb_encrypt((des_cblock *)(vp+ntpw_offs ),
                   (des_cblock *)md4, ks1, DES_DECRYPT);
    des_ecb_encrypt((des_cblock *)(vp+ntpw_offs + 8),
                   (des_cblock *)&md4[8], ks2, DES_DECRYPT);

    /* Decrypt the lanman password hash as two 8 byte blocks. */
    des_ecb_encrypt((des_cblock *)(vp+lmpw_offs),
                   (des_cblock *)lanman, ks1, DES_DECRYPT);
    des_ecb_encrypt((des_cblock *)(vp+lmpw_offs + 8),
                   (des_cblock *)&lanman[8], ks2, DES_DECRYPT);
      
    strncpy(newp, password, 16);
    newp[16]=0;
    pl=strlen(newp);
   
    if(0!=*newp)
        {
        cheap_ascii2uni(newp,newunipw,pl);
   
        make_lanmpw(newp,newlanpw,pl);

        MD4Init (&context);
        MD4Update (&context, newunipw, pl<<1);
        MD4Final (digest, &context);
     
        E1(newlanpw,   x1, lanman);
        E1(newlanpw+7, x1, lanman+8);
     
     
        /* Encrypt the NT md4 password hash as two 8 byte blocks. */
        des_ecb_encrypt((des_cblock *)digest,
                        (des_cblock *)despw, ks1, DES_ENCRYPT);
        des_ecb_encrypt((des_cblock *)(digest+8),
                        (des_cblock *)&despw[8], ks2, DES_ENCRYPT);
     
        des_ecb_encrypt((des_cblock *)lanman,
                        (des_cblock *)newlandes, ks1, DES_ENCRYPT);
        des_ecb_encrypt((des_cblock *)(lanman+8),
                        (des_cblock *)&newlandes[8], ks2, DES_ENCRYPT);
        }

    if(0==*newp)
        {
        /* Setting hash lengths to zero seems to make NT think it is blank
         * However, since we cant cut the previous hash bytes out of the V value
         * due to missing resize-support of values, it may leak about 40 bytes
         * each time we do this.
         */
        v->ntpw_len = 0;
        v->lmpw_len = 0;
        }
    else if(!all_pw_blank)
        {
        for (i = 0; i < 16; i++)
            {
            vp[ntpw_offs+i] = despw[i];
            if (lmpw_len >= 16)
                vp[lmpw_offs+i] = newlandes[i];
            }
        v->ntpw_len = 16;
        if (lmpw_len >= 16)
            v->lmpw_len = 16;
        }
    else
        {
        WriteLog("compute_pw: Unable to set since it is blank.\n");
        return 0;
        }

    WriteLog("Changed!\n");

    return(1);
    }

/* ============================================================== */

/* Crypto-stuff & support for what we'll do in the V-value */

/* Zero out string for lanman passwd, then uppercase
 * the supplied password and put it in here */

void make_lanmpw(char *p, unsigned char *lm, int len)
{
   int i;
   
   for (i=0; i < 15; i++) lm[i] = 0;
   for (i=0; i < len; i++) lm[i] = toupper(p[i]);
}

/*
 * Convert a 7 byte array into an 8 byte des key with odd parity.
 */

void str_to_key(unsigned char *str,unsigned char *key)
{
	int i;

	key[0] = str[0]>>1;
	key[1] = ((str[0]&0x01)<<6) | (str[1]>>2);
	key[2] = ((str[1]&0x03)<<5) | (str[2]>>3);
	key[3] = ((str[2]&0x07)<<4) | (str[3]>>4);
	key[4] = ((str[3]&0x0F)<<3) | (str[4]>>5);
	key[5] = ((str[4]&0x1F)<<2) | (str[5]>>6);
	key[6] = ((str[5]&0x3F)<<1) | (str[6]>>7);
	key[7] = str[6]&0x7F;
	for (i=0;i<8;i++) {
		key[i] = (key[i]<<1);
	}
	DES_set_odd_parity((des_cblock *)key);
}

/*
 * Function to convert the RID to the first decrypt key.
 */

void sid_to_key1(unsigned long sid,unsigned char deskey[8])
{
	unsigned char s[7];

	s[0] = (unsigned char)(sid & 0xFF);
	s[1] = (unsigned char)((sid>>8) & 0xFF);
	s[2] = (unsigned char)((sid>>16) & 0xFF);
	s[3] = (unsigned char)((sid>>24) & 0xFF);
	s[4] = s[0];
	s[5] = s[1];
	s[6] = s[2];

	str_to_key(s,deskey);
}

/*
 * Function to convert the RID to the second decrypt key.
 */

void sid_to_key2(unsigned long sid,unsigned char deskey[8])
{
	unsigned char s[7];
	
	s[0] = (unsigned char)((sid>>24) & 0xFF);
	s[1] = (unsigned char)(sid & 0xFF);
	s[2] = (unsigned char)((sid>>8) & 0xFF);
	s[3] = (unsigned char)((sid>>16) & 0xFF);
	s[4] = s[0];
	s[5] = s[1];
	s[6] = s[2];

	str_to_key(s,deskey);
}

/* DES encrypt, for LANMAN */

void E1(unsigned char *k, unsigned char *d, unsigned char *out)
{
  des_key_schedule ks;
  des_cblock deskey;

  str_to_key(k,(unsigned char *)deskey);
#ifdef __FreeBSD__
  des_set_key(&deskey,ks);
#else /* __FreeBsd__ */
  des_set_key((des_cblock *)deskey,ks);
#endif /* __FreeBsd__ */
  des_ecb_encrypt((des_cblock *)d,(des_cblock *)out, ks, DES_ENCRYPT);
}

