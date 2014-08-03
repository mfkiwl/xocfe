/*@
Copyright (c) 2013-2014, Su Zhenyu steven.known@gmail.com 
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Su Zhenyu nor the names of its contributors
      may be used to endorse or promote products derived from this software 
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE 
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
@*/
#include "cfecom.h"

CHAR * g_source_file_name = NULL;


UINT fe_process()
{
	INT s = c_parser();
	if (s != ST_SUCC) {
		return s;
	}
	s = type_trans();
	if (s != ST_SUCC) {
		return s;
	}
	s = type_ck();
	if (s != ST_SUCC) {
		return s;
	}	
	return s;
}


static bool is_c_source_file(CHAR * fn)
{
	CHAR * buf = (CHAR*)calloc(strlen(fn) + 1, 1);
	if (strcmp(upper(getfilesuffix(fn, buf)), "C") == 0) {
		return true;
	}
	return false;
}


static CHAR * process_d(INT argc, CHAR * argv[], IN INT & i)
{
	CHAR * n = NULL;
	if (i + 1 < argc && argv[i + 1] != NULL) {
		n = argv[i + 1];
	}
	i += 2;
	return n;
}


bool process_cmd(INT argc, CHAR * argv[])
{
	if (argc <= 1) return false;
	INT i = 1;
	while (i < argc) {
		if (argv[i][0] == '-') {
			CHAR const* cmdstr = &argv[i][1];
			if (!strcmp(cmdstr, "dump")) {
				CHAR * n = process_d(argc, argv, i);
				initdump(n, true);
			} else {
				return false;
			}
		} else if (is_c_source_file(argv[i])) {
			g_source_file_name = argv[i];
			g_hsrc = fopen(g_source_file_name, "rb");
			if (g_hsrc == NULL) {			
				fprintf(stdout, 
						"xoc: cannot open %s, error information is %s\n",
						g_source_file_name, strerror(errno));
				return false;
			}
			i++;
		}
	} //end while
	return true;
}


//cmdline usage: xocfe example.c -dump a.tmp
//#define DEBUG
#ifdef DEBUG
INT main(INT argcc, CHAR * argvc[])
{    
	CHAR * argv[] = {
		"xoc.exe",
		"../test/test_ansic.c",
	    "-dump","a.tmp",
	};
	INT argc = sizeof(argv)/sizeof(argv[0]);
#else
INT main(INT argc, CHAR * argv[])
{
#endif
	if (!process_cmd(argc, argv)) return 1;
	init_parser();
	g_fe_sym_tab = new SYM_TAB(64); 
	fe_process();

	//Show you all info that generated by CfrontEnd.
	dump_scope(get_global_scope(), 0xFFFFFFFF);	
	show_err();
	show_warn();
	fprintf(stdout, "\n%s - (%d) error(s), (%d) warnging(s)\n",
					g_source_file_name, 
					g_err_msg_list.get_elem_count(),
					g_warn_msg_list.get_elem_count());
	fini_parser();
	finidump();
	return 0;
}

