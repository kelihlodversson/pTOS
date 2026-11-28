/*
 * This file belongs to FreeMiNT. It's not in the original MiNT 1.12
 * distribution. See the file CHANGES for a detailed log of changes.
 *
 *
 * Copyright 2000 Frank Naumann <fnaumann@freemint.de>
 * All rights reserved.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * Author: Frank Naumann <fnaumann@freemint.de>
 * Started: 1999-07-27
 *
 * please send suggestions, patches or bug reports to me or
 * the MiNT mailing list
 *
 *
 * changes since last version:
 *
 * 1999-07-27:
 *
 * initial version; moved from biosfs.c
 * some cleanup
 *
 * known bugs:
 *
 * todo:
 *
 * optimizations to do:
 *
 */

/* Unchanged callback subset from FreeMiNT sys/dev-null.c at
 * 2afc6cd66501d6f1a959f5709626c3ffaae1ff56. */

long _cdecl
null_open (FILEPTR *f)
{
	UNUSED (f);

	return E_OK;
}

long _cdecl
null_write (FILEPTR *f, const char *buf, long bytes)
{
	UNUSED (f);
	UNUSED (buf);

	return bytes;
}

long _cdecl
null_read (FILEPTR *f, char *buf, long bytes)
{
	UNUSED (f);
	UNUSED (buf);
	UNUSED (bytes);

	return 0;
}

long _cdecl
null_lseek (FILEPTR *f, long where, int whence)
{
	UNUSED (f);
	UNUSED (whence);

	return (where == 0) ? 0 : EBADARG;
}

long _cdecl
null_close (FILEPTR *f, int pid)
{
	UNUSED (f);
	UNUSED (pid);

	return E_OK;
}
