/*
 * The new sysinstall program.
 *
 * This is probably the last program in the `sysinstall' line - the next
 * generation being essentially a complete rewrite.
 *
 * $Id: install.c,v 1.70.2.41 1995/06/10 07:58:37 jkh Exp $
 *
 * Copyright (c) 1995
 *	Jordan Hubbard.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    verbatim and that no modifications are made prior to this
 *    point in the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jordan Hubbard
 *	for the FreeBSD Project.
 * 4. The name of Jordan Hubbard or the FreeBSD project may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JORDAN HUBBARD ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JORDAN HUBBARD OR HIS PETS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, LIFE OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "sysinstall.h"
#include <sys/disklabel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

Boolean SystemWasInstalled = FALSE;

static Boolean	make_filesystems(void);
static Boolean	copy_self(void);
static Boolean	root_extract(void);

static Chunk *rootdev;

static Boolean
checkLabels(void)
{
    Device **devs;
    Disk *disk;
    Chunk *c1, *c2, *swapdev, *usrdev;
    int i;

    rootdev = swapdev = usrdev = NULL;
    devs = deviceFind(NULL, DEVICE_TYPE_DISK);
    /* First verify that we have a root device */
    for (i = 0; devs[i]; i++) {
	if (!devs[i]->enabled)
	    continue;
	disk = (Disk *)devs[i]->private;
	msgDebug("Scanning disk %s for root filesystem\n", disk->name);
	if (!disk->chunks)
	    msgFatal("No chunk list found for %s!", disk->name);
	for (c1 = disk->chunks->part; c1; c1 = c1->next) {
	    if (c1->type == freebsd) {
		for (c2 = c1->part; c2; c2 = c2->next) {
		    if (c2->type == part && c2->subtype != FS_SWAP && c2->private) {
			if (c2->flags & CHUNK_IS_ROOT) {
			    if (rootdev) {
				msgConfirm("WARNING:  You have more than one root device set?!\nUsing the first one found.");
				continue;
			    }
			    rootdev = c2;
			}
			else if (!strcmp(((PartInfo *)c2->private)->mountpoint, "/usr")) {
			    if (usrdev) {
				msgConfirm("WARNING:  You have more than one /usr filesystem.\nUsing the first one found.");
				continue;
			    }
			    usrdev = c2;
			}
		    }
		}
	    }
	}
    }

    /* Now check for swap devices */
    for (i = 0; devs[i]; i++) {
	disk = (Disk *)devs[i]->private;
	msgDebug("Scanning disk %s for swap partitions\n", disk->name);
	if (!disk->chunks)
	    msgFatal("No chunk list found for %s!", disk->name);
	for (c1 = disk->chunks->part; c1; c1 = c1->next) {
	    if (c1->type == freebsd) {
		for (c2 = c1->part; c2; c2 = c2->next) {
		    if (c2->type == part && c2->subtype == FS_SWAP) {
			swapdev = c2;
			break;
		    }
		}
	    }
	}
    }

    if (!rootdev) {
	msgConfirm("No root device found - you must label a partition as /\n in the label editor.");
	return FALSE;
    }
    else if (rootdev->name[strlen(rootdev->name) - 1] != 'a') {
	msgConfirm("Invalid placement of root partition.  For now, we only support\nmounting root partitions on \"a\" partitions due to limitations\nin the FreeBSD boot block code.  Please correct this and\ntry again.");
	return FALSE;
    }
    if (!swapdev) {
	msgConfirm("No swap devices found - you must create at least one\nswap partition.");
	return FALSE;
    }
    if (!usrdev)
	msgConfirm("WARNING:  No /usr filesystem found.  This is not technically\nan error if your root filesystem is big enough (or you later\nintend to get your /usr filesystem over NFS), but it may otherwise\ncause you trouble and is not recommended procedure!");
    return TRUE;
}

static Boolean
installInitial(void)
{
    extern u_char boot1[], boot2[];
    extern u_char mbr[], bteasy17[];
    u_char *mbrContents;
    Device **devs;
    int i;
    static Boolean alreadyDone = FALSE;

    if (alreadyDone)
	return TRUE;

    if (!getenv(DISK_PARTITIONED)) {
	msgConfirm("You need to partition your disk before you can proceed with\nthe installation.");
	return FALSE;
    }
    if (!getenv(DISK_LABELLED)) {
	msgConfirm("You need to assign disk labels before you can proceed with\nthe installation.");
	return FALSE;
    }
    if (!checkLabels())
	return FALSE;

    /* Figure out what kind of MBR the user wants */
    if (!dmenuOpenSimple(&MenuMBRType))
	return FALSE;

    switch (BootMgr) {
    case 0:
	mbrContents = bteasy17;
	break;

    case 1:
	mbrContents = mbr;
	break;

    case 2:
    default:
	mbrContents = NULL;
    }

    /* If we refuse to proceed, bail. */
    if (msgYesNo("Last Chance!  Are you SURE you want continue the installation?\n\nIf you're running this on an existing system, we STRONGLY\nencourage you to make proper backups before proceeding.\nWe take no responsibility for lost disk contents!"))
	return FALSE;

    devs = deviceFind(NULL, DEVICE_TYPE_DISK);
    for (i = 0; devs[i]; i++) {
	Chunk *c1;
	Disk *d = (Disk *)devs[i]->private;

	if (!devs[i]->enabled)
	    continue;

	if (mbrContents) {
	    Set_Boot_Mgr(d, mbrContents);
	    mbrContents = NULL;
	}
	Set_Boot_Blocks(d, boot1, boot2);
	msgNotify("Writing partition information to drive %s", d->name);
	Write_Disk(d);

	/* Now scan for bad blocks, if necessary */
	for (c1 = d->chunks->part; c1; c1 = c1->next) {
	    if (c1->flags & CHUNK_BAD144) {
		int ret;

		msgNotify("Running bad block scan on partition %s", c1->name);
		ret = vsystem("bad144 -v /dev/r%s 1234", c1->name);
		if (ret)
		    msgConfirm("Bad144 init on %s returned status of %d!", c1->name, ret);
		ret = vsystem("bad144 -v -s /dev/r%s", c1->name);
		if (ret)
		    msgConfirm("Bad144 scan on %s returned status of %d!", c1->name, ret);
	    }
	}
    }
    if (!make_filesystems()) {
	msgConfirm("Couldn't make filesystems properly.  Aborting.");
	return 0;
    }
    if (!copy_self()) {
	msgConfirm("Couldn't clone the boot floppy onto the root file system.\nAborting.");
	return 0;
    }
    dialog_clear();
    chroot("/mnt");
    chdir("/");
    variable_set2(RUNNING_ON_ROOT, "yes");
    /* stick a helpful shell over on the 4th VTY */
    if (OnVTY && !fork()) {
	int i, fd;
	extern int login_tty(int);

	msgDebug("Starting an emergency holographic shell over on the 4th screen\n");
	for (i = 0; i < 64; i++)
	    close(i);
	fd = open("/dev/ttyv3", O_RDWR);
	ioctl(0, TIOCSCTTY, &fd);
	dup2(0, 1);
	dup2(0, 2);
	if (login_tty(fd) == -1) {
	    msgNotify("Can't set controlling terminal");
	    exit(1);
	}
	printf("Warning: This shell is chroot()'d to /mnt\n");
	execlp("sh", "-sh", 0);
	exit(1);
    }
    alreadyDone = TRUE;
    return TRUE;
}

/*
 * What happens when we select "Install".  This is broken into a 3 stage installation so that
 * the user can do a full installation but come back here again to load more distributions,
 * perhaps from a different media type.  This would allow, for example, the user to load the
 * majority of the system from CDROM and then use ftp to load just the DES dist.
 */
int
installCommit(char *str)
{
    Device **devs;
    int i;

    if (!Dists) {
	msgConfirm("You haven't told me what distributions to load yet!\nPlease select a distribution from the Distributions menu.");
	return 0;
    }
    if (!mediaVerify())
	return 0;

    if (RunningAsInit && !SystemWasInstalled) {
	if (!installInitial())
	    return 0;
	configFstab();
    }
    if (!SystemWasInstalled && !root_extract()) {
	msgConfirm("Failed to load the ROOT distribution.  Please correct\nthis problem and try again.");
	return 0;
    }

    /* If we're about to extract the bin dist again, reset the installed state */
    if (Dists & DIST_BIN)
	SystemWasInstalled = FALSE;

    distExtractAll();

    if (!SystemWasInstalled && access("/kernel", R_OK)) {
	if (vsystem("ln -f /kernel.GENERIC /kernel")) {
	    msgConfirm("Unable to link /kernel into place!");
	    return 0;
	}
    }

    /* Resurrect /dev after bin distribution screws it up */
    if (!SystemWasInstalled) {
	msgNotify("Remaking all devices.. Please wait!");
	if (vsystem("cd /dev; sh MAKEDEV all"))
	    msgConfirm("MAKEDEV returned non-zero status");
	
	msgNotify("Resurrecting /dev entries for slices..");
	devs = deviceFind(NULL, DEVICE_TYPE_DISK);
	if (!devs)
	    msgFatal("Couldn't get a disk device list!");
	/* Resurrect the slices that the former clobbered */
	for (i = 0; devs[i]; i++) {
	    Disk *disk = (Disk *)devs[i]->private;
	    Chunk *c1;

	    if (!disk->chunks)
		msgFatal("No chunk list found for %s!", disk->name);
	    for (c1 = disk->chunks->part; c1; c1 = c1->next) {
		if (c1->type == freebsd) {
		    msgNotify("Making slice entries for %s", c1->name);
		    if (vsystem("cd /dev; sh MAKEDEV %sh", c1->name))
			msgConfirm("Unable to make slice entries for %s!", c1->name);
		}
	    }
	}
    }

    /* XXX Do all the last ugly work-arounds here which we'll try and excise someday right?? XXX */
    /* BOGON #1:  XFree86 extracting /usr/X11R6 with root-only perms */
    if (file_readable("/usr/X11R6"))
	(void)system("chmod 755 /usr/X11R6");

    /* BOGON #2: We leave /etc in a bad state */
    (void)system("chmod 755 /etc");

    dialog_clear();
    if (Dists)
	msgConfirm("Installation completed with some errors.  You may wish\nto scroll through the debugging messages on ALT-F2 with the scroll-lock\nfeature.  Press [ENTER] to return to the installation menu.");
    else
	msgConfirm("Installation completed successfully, now  press [ENTER] to return\nto the main menu. If you have any network devices you have not yet\nconfigured, see the Interface configuration item on the\nConfiguration menu.");
    SystemWasInstalled = TRUE;
    return 0;
}

/* Go newfs and/or mount all the filesystems we've been asked to */
static Boolean
make_filesystems(void)
{
    int i;
    Disk *disk;
    Chunk *c1, *c2;
    Device **devs;
    char dname[40];
    PartInfo *p = (PartInfo *)rootdev->private;
    Boolean RootReadOnly;

    command_clear();
    devs = deviceFind(NULL, DEVICE_TYPE_DISK);

    /* First, create and mount the root device */
    if (strcmp(p->mountpoint, "/"))
	msgConfirm("Warning: %s is marked as a root partition but is mounted on %s", rootdev->name, p->mountpoint);

    if (p->newfs) {
	int i;

	sprintf(dname, "/dev/r%sa", rootdev->disk->name);
	msgNotify("Making a new root filesystem on %s", dname);
	i = vsystem("%s %s", p->newfs_cmd, dname);
	if (i) {
	    msgConfirm("Unable to make new root filesystem!  Command returned status %d", i);
	    return FALSE;
	}
	RootReadOnly = FALSE;
    }
    else {
	RootReadOnly = TRUE;
	msgConfirm("Warning:  You have selected a Read-Only root device\nand may be unable to find the appropriate device entries on it\nif it is from an older pre-slice version of FreeBSD.");
	sprintf(dname, "/dev/r%sa", rootdev->disk->name);
	msgNotify("Checking integrity of existing %s filesystem", dname);
	i = vsystem("fsck -y %s", dname);
	if (i)
	    msgConfirm("Warning: fsck returned status off %d - this partition may be\nunsafe to use.", i);
    }
    sprintf(dname, "/dev/%sa", rootdev->disk->name);
    if (Mount("/mnt", dname)) {
	msgConfirm("Unable to mount the root file system!  Giving up.");
	return FALSE;
    }

    /* Now buzz through the rest of the partitions and mount them too */
    for (i = 0; devs[i]; i++) {
	if (!devs[i]->enabled)
	    continue;

	disk = (Disk *)devs[i]->private;
	if (!disk->chunks) {
	    msgConfirm("No chunk list found for %s!", disk->name);
	    return FALSE;
	}

	/* Make the proper device mount points in /mnt/dev */
	if (!(RootReadOnly && disk == rootdev->disk)) {
	    Mkdir("/mnt/dev", NULL);
	    MakeDevDisk(disk, "/mnt/dev");
	}
	for (c1 = disk->chunks->part; c1; c1 = c1->next) {
	    if (c1->type == freebsd) {
		for (c2 = c1->part; c2; c2 = c2->next) {
		    if (c2->type == part && c2->subtype != FS_SWAP && c2->private) {
			PartInfo *tmp = (PartInfo *)c2->private;

			if (!strcmp(tmp->mountpoint, "/"))
			    continue;

			if (tmp->newfs)
			    command_shell_add(tmp->mountpoint, "%s /mnt/dev/r%s", tmp->newfs_cmd, c2->name);
			else
			    command_shell_add(tmp->mountpoint, "fsck -y /mnt/dev/r%s", c2->name);
			command_func_add(tmp->mountpoint, Mount, c2->name);
		    }
		    else if (c2->type == part && c2->subtype == FS_SWAP) {
			char fname[80];
			int i;

			sprintf(fname, "/mnt/dev/%s", c2->name);
			i = swapon(fname);
			if (!i)
			    msgNotify("Added %s as a swap device", fname);
			else
			    msgConfirm("Unable to add %s as a swap device: %s", fname, strerror(errno));
		    }
		}
	    }
	    else if (c1->type == fat && c1->private && !RootReadOnly) {
		char name[FILENAME_MAX];

		sprintf(name, "/mnt%s", ((PartInfo *)c1->private)->mountpoint);
		Mkdir(name, NULL);
	    }
	}
    }

    /* Copy the boot floppy's dev files */
    if (vsystem("find -x /dev | cpio -pdmV /mnt")) {
	msgConfirm("Couldn't clone the /dev files!");
	return FALSE;
    }
    
    command_sort();
    command_execute();
    return TRUE;
}

/* Copy the boot floppy contents into /stand */
static Boolean
copy_self(void)
{
    int i;

    msgWeHaveOutput("Copying the boot floppy to /stand on root filesystem");
    i = vsystem("find -x /stand | cpio -pdmV /mnt");
    if (i) {
	msgConfirm("Copy returned error status of %d!", i);
	return FALSE;
    }

    /* Copy the /etc files into their rightful place */
    if (vsystem("cd /mnt/stand; find etc | cpio -pdmV /mnt")) {
	msgConfirm("Couldn't copy up the /etc files!");
	return TRUE;
    }
    return TRUE;
}

static Boolean loop_on_root_floppy(void);

static Boolean
root_extract(void)
{
    int fd;
    static Boolean alreadyExtracted = FALSE;

    if (alreadyExtracted)
	return TRUE;

    if (mediaDevice) {
	if (isDebug())
	    msgDebug("Attempting to extract root image from %s device\n", mediaDevice->description);
	switch(mediaDevice->type) {

	case DEVICE_TYPE_FLOPPY:
	    alreadyExtracted = loop_on_root_floppy();
	    break;

	default:
	    if (!(*mediaDevice->init)(mediaDevice))
		break;
	    fd = (*mediaDevice->get)(mediaDevice, "floppies/root.flp", NULL);
	    if (fd < 0) {
		msgConfirm("Couldn't get root image from %s!\nWill try to get it from floppy.", mediaDevice->name);
		(*mediaDevice->shutdown)(mediaDevice);
	        alreadyExtracted = loop_on_root_floppy();
	    }
	    else {
		msgNotify("Loading root image from %s", mediaDevice->name);
		alreadyExtracted = mediaExtractDist("/", fd);
		(*mediaDevice->close)(mediaDevice, fd);
	    }
	    break;
	}
    }
    else
	alreadyExtracted = loop_on_root_floppy();
    return alreadyExtracted;
}

static Boolean
loop_on_root_floppy(void)
{
    int fd;
    int status = FALSE;

    while (1) {
	fd = getRootFloppy();
	if (fd != -1) {
	    msgNotify("Extracting root floppy..");
	    status = mediaExtractDist("/", fd);
	    close(fd);
	    break;
	}
    }
    return status;
}
