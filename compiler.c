#include<stdlib.h>
#include<stdio.h>
#include<string.h>

#ifdef _WIN32
#define OS_WIN 1
#endif

#ifdef _WIN64
#define OS_WIN 1
#endif

#ifndef OS_WIN
#define OS_LINUX 1
#endif

/***************************/

#ifdef OS_LINUX
#include<stdarg.h>
#include<unistd.h>
#include<pthread.h>
#endif

#ifdef OS_WIN
#include<errno.h>
#include<io.h>
#include<ctype.h>
#include<direct.h>
#include<windows.h>
#include<setjmp.h>
#include<intrin.h>
#include<sys/timeb.h>

#include"winpthread.h"
#endif


#define __FILENAME__ "compiler.c"

struct s_flux {
        int val;
        int cpt;
};

struct s_parameter {
	char *filename1;
	char *filename2;
	char *label;
	int label_unique_index;
	int label_unique_index_start;
	int compilefull;
	int compilediff;
	int unbalanced,divint;
	// data
	int *idx1;
	int nidx1,midx1;
	int *idx2;
	int nidx2,midx2;
	// global options
	int jpix, jpiy, exhlde, jpmode, noret;
	char *absolute;
	int meta,skipblank;
	int autosplit;
	// int safe option
	int inch;
	// stack generator
	int stack,noSPbackup;
};

// v3
enum e_action_cache {
	E_AC_NOTHING=0,
	E_AC_LDBC=1,
	E_AC_LDDE=2,
	E_AC_LDHL=3,
	E_AC_INCA_unused=6,
	E_AC_INCB=7,
	E_AC_INCC=8,
	E_AC_INCD=9,
	E_AC_INCE=10,
	E_AC_INCH=11,
	E_AC_INCL=12,
	E_AC_LDB_A=13,
	E_AC_LDC_A=14,
	E_AC_LDD_A=15,
	E_AC_LDE_A=16,
	E_AC_LDH_A=17,
	E_AC_LDL_A=18,
	E_AC_LDB_C=19,
	E_AC_LDB_D=20,
	E_AC_LDB_E=21,
	E_AC_LDB_H=22,
	E_AC_LDB_L=23,
	E_AC_LDC_B=24,
	E_AC_LDC_D=25,
	E_AC_LDC_E=26,
	E_AC_LDC_H=27,
	E_AC_LDC_L=28,
	E_AC_LDD_B=29,
	E_AC_LDD_C=30,
	E_AC_LDD_E=31,
	E_AC_LDD_H=32,
	E_AC_LDD_L=33,
	E_AC_LDE_B=34,
	E_AC_LDE_C=35,
	E_AC_LDE_D=36,
	E_AC_LDE_H=37,
	E_AC_LDE_L=38,
	E_AC_LDH_B=39,
	E_AC_LDH_C=40,
	E_AC_LDH_D=41,
	E_AC_LDH_E=42,
	E_AC_LDH_L=43,
	E_AC_LDL_B=44,
	E_AC_LDL_C=45,
	E_AC_LDL_D=46,
	E_AC_LDL_E=47,
	E_AC_LDL_H=48,
	E_AC_LDB_n=49,
	E_AC_LDC_n=50,
	E_AC_LDD_n=51,
	E_AC_LDE_n=52,
	E_AC_LDH_n=53,
	E_AC_LDL_n=54,
	E_AC_LDA_n=55,
	E_AC_DECB=56,
	E_AC_DECC=57,
	E_AC_DECD=58,
	E_AC_DECE=59,
	E_AC_DECH=60,
	E_AC_DECL=61,
	E_AC_END=62
};
enum e_push_register {
	E_PUSH_NOTHING=0,
	E_PUSH_BC=1,
	E_PUSH_DE=2,
	E_PUSH_HL=3,
	E_PUSH_END=6
};
struct s_action_cache {
	enum e_action_cache action;
	enum e_push_register preg;
	int totalcost,size,remaining;
        int rA,rB,rC,rD,rE,rH,rL;
	struct s_action_cache *next;
	struct s_action_cache *previous;
};

void IntArrayAddDynamicValueConcat(int **zearray, int *nbval, int *maxval, int zevalue)
{
        if ((*zearray)==NULL) {
                *nbval=1;
                *maxval=4;
                (*zearray)=malloc(sizeof(int)*(*maxval));
        } else {
                *nbval=*nbval+1;
                if (*nbval>=*maxval) {
                        *maxval=(*maxval)*2;
                        (*zearray)=realloc((*zearray),sizeof(int)*(*maxval));
                }
        }
        (*zearray)[(*nbval)-1]=zevalue;
}


/************************ multi-threaded execution *************************************************/

void ObjectArrayAddDynamicValueConcat(void **zearray, int *nbfields, int *maxfields, void *zeobject, int object_size)
{
	#undef FUNC
	#define FUNC "ObjectArrayAddDynamicValueConcat"

	char *dst;

	if ((*zearray)==NULL) {
		*nbfields=1;
		*maxfields=8;
		(*zearray)=malloc((*maxfields)*object_size);
	} else {
		*nbfields=(*nbfields)+1;
		if (*nbfields>=*maxfields) {
			*maxfields=(*maxfields)*2;
			(*zearray)=realloc((*zearray),(*maxfields)*object_size);
		}
	}
	/* using direct calls because it is more interresting to know which is the caller */
	dst=((char *)(*zearray))+((*nbfields)-1)*object_size;
	/* control of memory for overflow */
	memcpy(dst,zeobject,object_size);
}


int _internal_CPUGetCoreNumber()
{
        #undef FUNC
        #define FUNC "_internal_CPUGetCoreNumber"

        static int nb_cores=0;
#ifdef OS_WIN
		SYSTEM_INFO sysinfo;
		if (!nb_cores) {
			GetSystemInfo( &sysinfo );
			nb_cores=sysinfo.dwNumberOfProcessors;
			if (nb_cores<=0)
					nb_cores=1;
		}
#else
        if (!nb_cores) {
                nb_cores=sysconf(_SC_NPROCESSORS_ONLN );
                if (nb_cores<=0)
                        nb_cores=1;
        }
#endif
        return nb_cores;
}
#define CPU_NB_CORES _internal_CPUGetCoreNumber()

void diff_printf(char **output, int *lenoutput,...)
{
	#undef FUNC
	#define FUNC "diff_printf"
	
	char tmp[2048];
	int curlen;
	char *format;
	va_list argptr;

	va_start(argptr,lenoutput);
	format=va_arg(argptr,char *);
	vsprintf(tmp,format,argptr);
	curlen=strlen(tmp); // windows compliant...
	va_end(argptr);
	
	*output=realloc(*output,(*lenoutput)+curlen+1);
//printf("input=[%s] tmp=[%s] curlen=%d\n",*output,tmp,curlen);
	memcpy((*output)+*lenoutput,tmp,curlen);
//printf("output=[%s]\n",*output);
	*lenoutput=(*lenoutput)+curlen;
}


struct s_compilation_action {
	unsigned char *sp1;
	unsigned char *sp2;
	unsigned char *mask;
	int idx,idx2;
	int wrklen;
	int uidx;
	int codeSize;
};

struct s_compilation_thread {
	pthread_t thread;
	struct s_compilation_action *action;
	int nbaction,maxaction;
	struct s_parameter *parameter;
	char *output;
	int lenoutput;
};

struct s_compilation_thread *SplitForThreads(int nbwork, struct s_compilation_action *ca, int *nb_cores, struct s_parameter *parameter)
{
	#undef FUNC
	#define FUNC "SplitForThread"

	struct s_compilation_thread *compilation_threads;
	int i,j,curidx,step;

	/* sometimes we do not want to split the work into the exact number of cores */
	if (!*nb_cores) {
		*nb_cores=_internal_CPUGetCoreNumber();
	}
	compilation_threads=calloc(1,sizeof(struct s_compilation_thread)*(*nb_cores));

	/* dispatch workload */
	step=nbwork/(*nb_cores);
	while (step<2 && *nb_cores>2) {
		*nb_cores=*nb_cores>>1;
		step=nbwork/(*nb_cores);
	}
	if (step<1) step=1;

	while (step*(*nb_cores)<nbwork) step++;

	fprintf(stderr,"split %d tasks with %d core%s and average step of %d\n",nbwork,*nb_cores,*nb_cores>1?"s":"",step);

	/* split image into strips */
	for (curidx=i=0;i<*nb_cores;i++) {
		compilation_threads[i].parameter=parameter;
		for (j=0;j<step && curidx<nbwork;j++) {
			ObjectArrayAddDynamicValueConcat((void **)&compilation_threads[i].action,&compilation_threads[i].nbaction,&compilation_threads[i].maxaction,&ca[curidx++],sizeof(struct s_compilation_action));
		}
	}

	return compilation_threads;
}

void MakeDiffStack(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux, int *codesize);
void MakeDiff(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux, int *codesize);

void *MakeDiffThread(void *param)
{
	#undef FUNC
	#define FUNC "MakeDiffThread"

	int i,j;
	struct s_compilation_thread *ct;
	ct=(struct s_compilation_thread *)param;
	for (i=0;i<ct->nbaction;i++) {
		for (j=0;j<ct->action[i].wrklen;j++) {
			ct->action[i].sp1[j]&=0xF;
			ct->action[i].sp2[j]&=0xF;
		}
		if (ct->parameter->label) {
			if (ct->parameter->label_unique_index) {
				diff_printf(&ct->output,&ct->lenoutput,"%s%d:\n",ct->parameter->label,ct->action[i].uidx);
			} else {
				if (ct->action[i].idx==ct->action[i].idx2) diff_printf(&ct->output,&ct->lenoutput,"%s%d:\n",ct->parameter->label,ct->action[i].idx);
					else diff_printf(&ct->output,&ct->lenoutput,"%s%d_%d:\n",ct->parameter->label,ct->action[i].idx,ct->action[i].idx2);
			}
		}
		if (ct->parameter->stack)
			MakeDiffStack(&ct->output, &ct->lenoutput,ct->parameter,ct->action[i].sp1,ct->action[i].sp2,ct->action[i].wrklen,&ct->action[i].codeSize);
		else
			MakeDiff(&ct->output, &ct->lenoutput,ct->parameter,ct->action[i].sp1,ct->action[i].sp2,ct->action[i].wrklen,&ct->action[i].codeSize);
	}
	pthread_exit(NULL);
	return NULL;
}

void ExecuteThreads(int nb_cores, struct s_compilation_thread *compilation_threads, void *(*fct)(void *))
{
	#undef FUNC
	#define FUNC "ExecuteThreads"

	pthread_attr_t attr;
	void *status;
	int i,rc;
	/* launch threads */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
	pthread_attr_setstacksize(&attr,65536);
 	for (i=0;i<nb_cores;i++) {
		if ((rc=pthread_create(&compilation_threads[i].thread,&attr,fct,(void *)&compilation_threads[i]))) {
			fprintf(stderr,"Cannot create thread!");
			exit(-5);
		}
	}
 	for (i=0;i<nb_cores;i++) {
		if ((rc=pthread_join(compilation_threads[i].thread,&status))) {
			fprintf(stderr,"thread error!");
			exit(-5);
		}
	}
}





void __FileReadBinary(char *filename, char *data, int len, int curline)
{
	FILE *f;

	f=fopen(filename,"rb");
	if (!f) {
		printf("(%d) file [%s] not found or cannot be opened!\n",curline,filename);
		exit(-1);
	}
	if (fread(data,1,len,f)!=len) {
		printf("(%d) file [%s] cannot read %d byte(s)!\n",curline,filename,len);
		exit(-1);
	}
	fclose(f);
}
#define FileReadBinary(fichier,data,len) __FileReadBinary(fichier,data,len,__LINE__)

#define NBREG 5

char *GetValStr(char *txtbuffer, int *current_reg,int v) {
        #undef FUNC
        #define FUNC "GetValStr"

	static char *txtreg[NBREG]={"b","c","d","e","a"};
	int i;

	for (i=0;i<NBREG;i++) {
		if (v==current_reg[i] && current_reg[i]!=-1) {
			sprintf(txtbuffer,"%s",txtreg[i]);
			return txtbuffer;
		}
	}
	sprintf(txtbuffer,"#%X",v);
	return txtbuffer;
}

/*
struct s_sprval {
	int val;
	int cpt;
	int prv;
};
struct s_regupdate {
	int reg;
	int val;
	int oldval;
};
int compare_sprval(const void *a, const void *b)
{
	struct s_sprval *sa,*sb;
	sa=(struct s_sprval *)a;
	sb=(struct s_sprval *)b;
	// du plus grand au plus petit
	return sb->cpt-sa->cpt;
}
*/

// optimizer v2
struct s_cell {
        struct s_cell *prev,*next;
        int val;
        int cpt;
};

int compare_fluxcount(const void *a, const void *b)
{
        struct s_flux *sa,*sb;
        sa=(struct s_flux *)a;
        sb=(struct s_flux *)b;
        /* du plus grand au plus petit */
        return sb->cpt-sa->cpt;
}


void register_update(char **output, int *lenoutput, char **reg,unsigned char *flux, int start, int rmax, int *cval) {
	int rvalue,ovalue;
	rvalue=flux[start];
	ovalue=cval[rmax];
	if (strcmp(reg[rmax],"a")==0) { // optimisations propre à A
		if (!rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ xor a ; idx=%d\n",start); else
		if (((ovalue+cval[0])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ add b ; idx=%d\n",start); else
		if (((ovalue+cval[1])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ add c ; idx=%d\n",start); else
		if (((ovalue+cval[2])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ add d ; idx=%d\n",start); else
		if (((ovalue+cval[3])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ add e ; idx=%d\n",start); else
		if (((ovalue-cval[0])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ sub b ; idx=%d\n",start); else
		if (((ovalue-cval[1])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ sub c ; idx=%d\n",start); else
		if (((ovalue-cval[2])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ sub d ; idx=%d\n",start); else
		if (((ovalue-cval[3])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ sub e ; idx=%d\n",start); else
		if (((ovalue^cval[0])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ xor b ; idx=%d\n",start); else
		if (((ovalue^cval[1])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ xor c ; idx=%d\n",start); else
		if (((ovalue^cval[2])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ xor d ; idx=%d\n",start); else
		if (((ovalue^cval[3])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ xor e ; idx=%d\n",start); else
		if (((ovalue&cval[0])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ and b ; idx=%d\n",start); else
		if (((ovalue&cval[1])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ and c ; idx=%d\n",start); else
		if (((ovalue&cval[2])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ and d ; idx=%d\n",start); else
		if (((ovalue&cval[3])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ and e ; idx=%d\n",start); else
		if (((ovalue|cval[0])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ or b ; idx=%d\n",start); else
		if (((ovalue|cval[1])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ or c ; idx=%d\n",start); else
		if (((ovalue|cval[2])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ or d ; idx=%d\n",start); else
		if (((ovalue|cval[3])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ or e ; idx=%d\n",start); else
		if (((ovalue+cval[4])&0xFF)==rvalue) diff_printf(output,lenoutput,"\n/* stat update a */ add a ; idx=%d\n",start); else
		// legacy
		if (((rvalue+1)&0xFF)==ovalue) diff_printf(output,lenoutput,"\n/* stat update */ dec %s ; idx=%d\n",reg[rmax],start); else
		if (((rvalue-1)&0xFF)==ovalue) diff_printf(output,lenoutput,"\n/* stat update */ inc %s ; idx=%d\n",reg[rmax],start); else
						diff_printf(output,lenoutput,"\n/* stat update */ ld %s,#%02X ; idx=%d\n",reg[rmax],rvalue,start);
	} else {

		if (((rvalue+1)&0xFF)==ovalue) diff_printf(output,lenoutput,"\n/* stat update */ dec %s ; idx=%d\n",reg[rmax],start); else
		if (((rvalue-1)&0xFF)==ovalue) diff_printf(output,lenoutput,"\n/* stat update */ inc %s ; idx=%d\n",reg[rmax],start); else
						diff_printf(output,lenoutput,"\n/* stat update */ ld %s,#%02X ; idx=%d\n",reg[rmax],rvalue,start);
	}
	cval[rmax]=flux[start];
}

int *ComputeDiffStats(char **output, int *lenoutput,unsigned char *flux, int start, int size, int *cval) {
        struct s_flux fcount[256];
        struct s_flux fbefor[256];
        int i,j,k,l,mreg;
        char *reg[]={"b","c","d","e","a"};

int dibiji=0;

        if (!start) {
                for (i=0;i<NBREG;i++) {
                        cval[i]=-1;
                }
        }
        // count bytes
        for (i=0;i<256;i++) {
                fcount[i].val=i;
                fcount[i].cpt=0;
                fbefor[i].val=i;
                fbefor[i].cpt=0;
        }
        for (i=start;i<size;i++) {
                fcount[flux[i]].cpt++;
        }
        qsort(fcount,256,sizeof(struct s_flux),compare_fluxcount);

        // first init avec les premières données à dépasser le seuil
        if (!start) {
                for (i=mreg=0;i<size;i++) {
                        fbefor[flux[i]].cpt++;
			// we want more than a simple threshold because we MAY change register value during the whole data
                        if (fbefor[flux[i]].cpt>4) {
                                // init!
                                cval[mreg++]=flux[i];
                                fbefor[flux[i]].cpt=-size-1; // annuler les prochains tests
                                if (mreg==NBREG) break;
                        }
                }
                // reinit for 2nd pass with a lower treshold (at least, minimal treshold)
                for (i=0;i<256;i++) {
                        if (fbefor[i].cpt>=0) fbefor[i].cpt=0; // mais pas les valeurs déjà validées
                        else fbefor[i].cpt-=size; // on en remet un petit coup!
                }
                for (i=0;i<size;i++) {
                        fbefor[flux[i]].cpt++;
                        if (fbefor[flux[i]].cpt>2) {
                                // init!
                                cval[mreg++]=flux[i];
                                fbefor[flux[i]].cpt=-size; // annuler les prochains tests
                                if (mreg==NBREG) break;
                        }
                }
		// automatic pack
                i=0;
                while (i+1<NBREG) {
                        if (cval[i]>=0 && cval[i+1]>=0) {
                                diff_printf(output,lenoutput,"ld %s%s,#%02X%02X\n",reg[i],reg[i+1],cval[i],cval[i+1]);
                        } else if (cval[i]>=0) {
                                diff_printf(output,lenoutput,"ld %s,#%02X\n",reg[i],cval[i]);
                        }
                        i+=2;
                }
                if (i<NBREG) {
                        if (cval[i]>=0) diff_printf(output,lenoutput,"ld %s,#%02X\n",reg[i],cval[i]);
                }
        } else {
                int inplace,imax,rmax,tresh,ireplace;

                // tester si la valeur courante est en cache
                for (i=inplace=0;i<NBREG && cval[i]!=-1;i++) {
                        if (flux[start]==cval[i]) {
                                inplace=1;
                                break;
                        }
                }

                /* on va, pour toutes les valeurs >4 qui ne sont pas déjà en cache,
                 * regarder si on peut se substituer à un des caches existants
                 */

                // si elle n'est pas en cache alors on va voir si on peut changer un registre, celui qui est le plus loin de la position courante
                if (!inplace) {
                        // retrouver la valeur courante dans le tri
                        for (i=0;i<256;i++) {
                                if (fcount[i].val==flux[start]) break;
                        }
                        ireplace=i;

                        // en fonction de la quantité on change le seuil (car on pourrait avoir besoin de rebasculer sur une autre valeur et ça a un coût)
                        switch (fcount[NBREG-1].cpt) {
                                case 0:tresh=2;break;
                                case 1:tresh=3;break;
                                default:tresh=4;break;
                        }

                        // est-ce que la valeur dont on a besoin a intérêt à être remplacée? est-ce qu'on dépasse notre seuil?
                        if (fcount[ireplace].cpt>tresh) {
                                if (dibiji) printf("valeur #%02X on dépasse notre seuil %d>%d\n",fcount[ireplace].val,fcount[ireplace].cpt,tresh);

                                // on cherche parmi les valeurs en cache celle qui est le plus loin possible de la courante pour augmenter nos chances de remplacement
                                for (j=imax=0;j<NBREG;j++) {
                                        for (i=start;i<size;i++) {
                                                if (flux[i]!=cval[j]) {
						       	if (i>imax) {
	                                                        imax=i;
        	                                                rmax=j;
							}
                                                } else break;
                                        }
                                }
                                if (dibiji) printf(" rmax=%d candidate register=%s imax=%d\n",rmax,reg[rmax],imax);

                                // ensuite on va analyser la distribution dans le flux au plus proche pour voir si c'est possible de remplacer
                                for (i=start;i<size;i++) {
                                        fbefor[flux[i]].cpt++;

                                        // tant que la valeur à remplacer ne dépasse pas le seuil
                                        if (fbefor[cval[rmax]].cpt<tresh+1) {
                                                // si la valeur en cache dépasse ou égalise la plus balaise avant le seuil, alors on n'en tient pas compte
                                                if (fbefor[cval[rmax]].cpt>=fbefor[flux[start]].cpt) {
                                                        if (dibiji) printf(" valeur en cache > nouvelle candidate avant le seuil\n");
                                                        break;
                                                }
                                        } else {
						// on a un premier feu vert pour remplacer la valeur par la nouvelle MAIS
						// est-ce qu'il n'existerait pas d'autres valeurs +intéressantes sur l'intervale?
						// qui ne soient pas en cache...
						for (l=0;l<256;l++) {
							if (l==flux[start]) continue;
							if (fbefor[l].cpt>fbefor[flux[start]].cpt) {
								// on a un meilleur candidat sur l'intervale!
								for (j=0;j<NBREG;j++) {
									if (cval[j]==l) break;
								}
								// et il n'est pas en cache!
								if (j==NBREG) {
									if (dibiji) printf("pas de remplacement car on a mieux sur la période!\n");
									break;
								}
							}
						}
						if (l==256) {
							if (dibiji) printf("stat update because #%02X tresh at %d\n",fbefor[cval[rmax]].val,i);
        	                                        // la nouvelle valeur dépasse le seuil d'occurences
        		                        	register_update(output,lenoutput,reg,flux,start,rmax,cval);
						}
						// else break dans tous les cas!
                                                break;
                                        }
                                }
				if (i==size) {
	                                if (dibiji) printf("analyse de flux n'a pas rencontré assez de valeurs à remplacer,c'est qu'on peut écraser! (ou pas!)\n");
					for (l=0;l<256;l++) {
						if (l==flux[start]) continue;
						if (fbefor[l].cpt>fbefor[flux[start]].cpt) {
							// on a un meilleur candidat sur l'intervale!
							for (j=0;j<NBREG;j++) {
								if (cval[j]==l) break;
							}
							// et il n'est pas en cache!
							if (j==NBREG) {
								if (dibiji) printf("pas de remplacement car on a mieux sur la période!\n");
								break;
							}
						}
					}
					if (l==256) {
						if (dibiji) printf("validé sur la période on remplace\n");
        		                        register_update(output,lenoutput,reg,flux,start,rmax,cval);
					}
				}
                        } else {
                                if (dibiji) printf("valeur courante pas en cache et pas intéressante (x%d)\n",fcount[ireplace].cpt);
                        }
                }
        }

        return cval;
}



					/* optimised A v1.4 
					ia=i;
					if (!regupdate[ia].val) {
						diff_printf(output,lenoutput,"xor a\n");
					} else if (regupdate[ia].val==((valregA+valregA)&0xFF)) {
						diff_printf(output,lenoutput,"add a\n");
					} else if (valregB!=-1 && regupdate[ia].val==((valregA+valregB)&0xFF)) {
						diff_printf(output,lenoutput,"add b\n");
					} else if (valregC!=-1 && regupdate[ia].val==((valregA+valregC)&0xFF)) {
						diff_printf(output,lenoutput,"add c\n");
					} else if (valregD!=-1 && regupdate[ia].val==((valregA+valregD)&0xFF)) {
						diff_printf(output,lenoutput,"add d\n");
					} else if (valregE!=-1 && regupdate[ia].val==((valregA+valregE)&0xFF)) {
						diff_printf(output,lenoutput,"add e\n");
					} else if (valregB!=-1 && regupdate[ia].val==((valregA-valregB)&0xFF)) {
						diff_printf(output,lenoutput,"sub b\n");
					} else if (valregC!=-1 && regupdate[ia].val==((valregA-valregC)&0xFF)) {
						diff_printf(output,lenoutput,"sub c\n");
					} else if (valregD!=-1 && regupdate[ia].val==((valregA-valregD)&0xFF)) {
						diff_printf(output,lenoutput,"sub d\n");
					} else if (valregE!=-1 && regupdate[ia].val==((valregA-valregE)&0xFF)) {
						diff_printf(output,lenoutput,"sub e\n");
					} else if (regupdate[ia].val==(valregA|valregB)) {
						diff_printf(output,lenoutput,"or b\n");
					} else if (regupdate[ia].val==(valregA|valregC)) {
						diff_printf(output,lenoutput,"or c\n");
					} else if (regupdate[ia].val==(valregA|valregD)) {
						diff_printf(output,lenoutput,"or d\n");
					} else if (regupdate[ia].val==(valregA|valregE)) {
						diff_printf(output,lenoutput,"or e\n");
					} else if (regupdate[ia].val==(valregA^valregB)) {
						diff_printf(output,lenoutput,"xor b\n");
					} else if (regupdate[ia].val==(valregA^valregC)) {
						diff_printf(output,lenoutput,"xor c\n");
					} else if (regupdate[ia].val==(valregA^valregD)) {
						diff_printf(output,lenoutput,"xor d\n");
					} else if (regupdate[ia].val==(valregA^valregE)) {
						diff_printf(output,lenoutput,"xor e\n");
					} else if (regupdate[ia].val==(valregA&valregB)) {
						diff_printf(output,lenoutput,"and b\n");
					} else if (regupdate[ia].val==(valregA&valregC)) {
						diff_printf(output,lenoutput,"and c\n");
					} else if (regupdate[ia].val==(valregA&valregD)) {
						diff_printf(output,lenoutput,"and d\n");
					} else if (regupdate[ia].val==(valregA&valregE)) {
						diff_printf(output,lenoutput,"and e\n");
					} else {
						diff_printf(output,lenoutput,"ld a,%d\n",regupdate[i].val);
					}
					valregA=regupdate[ia].val;
					break;
					*/
/*
struct s_action_cache {
	enum e_action_cache action;
	enum e_push_register preg;
	int totalcost;
        unsigned short int LDBC;
        unsigned short int LDDE;
        unsigned short int LDHL;
        unsigned short int LDIX;
        unsigned short int LDIY;
        unsigned char LDA,LDB,LDC,LDD,LDE,LDH,LDL,LDXL,LDXH,LDXL,LDYH,LDYL;
	struct s_action_cache *next;
};
*/

void BrutForce(struct s_action_cache *previous, unsigned char *flux, int len) {
	static int cpt=0;
	struct s_action_cache *current;
	unsigned short int newval;
	unsigned char lowval,highval;
	int i,j,skip;


	cpt++;
if (cpt>840000) {
	if (cpt==840001) printf("STOP!\n");
	cpt++;
	return;
}

if (previous->totalcost>7*256) {
	printf("absurd code! too merdique!\n");
	return;
}

	// init new actions
	current=calloc(E_AC_END,sizeof(struct s_action_cache)); // malloc+memset
	previous->next=current;


	for (i=0;i<E_AC_END;i++) {
		current[i]=previous[0]; // copy current state to all news branches
		current[i].action=E_AC_NOTHING; // no action as default
		current[i].preg=E_PUSH_NOTHING; // no push as default
		current[i].next=NULL; // end all branches as default
		current[i].previous=previous;
		current[i].remaining=len;
	}

	lowval=flux[0];
	highval=flux[1];

	i=0;
	if (current[i].rC==lowval && current[i].rB==highval) { current[i].size++; current[i].totalcost+=4; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); return; } // already in BC
	if (current[i].rE==lowval && current[i].rD==highval) { current[i].size++; current[i].totalcost+=4; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); return; } // already in DE
	if (current[i].rL==lowval && current[i].rH==highval) { current[i].size++; current[i].totalcost+=4; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); return; } // already in HL
	// not in a 16 bits register, we must load something!
	
	// do we have ONE well placed data anywhere?
	if (current[i].rC==lowval || current[i].rE==lowval || current[i].rL==lowval || current[i].rB==highval || current[i].rD==highval || current[i].rH==highval) {
		// we can have what we want with a single LD (or not!)
		if (current[i].rC==lowval) {
			skip=0;
			if (current[i].rC==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rB=highval; current[i].action=E_AC_LDB_C; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rD==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rB=highval; current[i].action=E_AC_LDB_D; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rE==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rB=highval; current[i].action=E_AC_LDB_E; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rH==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rB=highval; current[i].action=E_AC_LDB_H; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rL==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rB=highval; current[i].action=E_AC_LDB_L; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rA==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rB=highval; current[i].action=E_AC_LDB_A; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && ((current[i].rB+1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rB=highval;current[i].action=E_AC_INCB; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip && ((current[i].rB-1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rB=highval;current[i].action=E_AC_DECB; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip) { current[i].totalcost+=2+4; current[i].size+=3; current[i].rB=highval; current[i].action=E_AC_LDB_n; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; }
		}
		if (current[i].rE==lowval) {
			skip=0;
			if (current[i].rB==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rD=highval; current[i].action=E_AC_LDD_B; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rC==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rD=highval; current[i].action=E_AC_LDD_C; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rE==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rD=highval; current[i].action=E_AC_LDD_E; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rH==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rD=highval; current[i].action=E_AC_LDD_H; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rL==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rD=highval; current[i].action=E_AC_LDD_L; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rA==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rD=highval; current[i].action=E_AC_LDD_A; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && ((current[i].rD+1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rD=highval;current[i].action=E_AC_INCD; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip && ((current[i].rD-1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rD=highval;current[i].action=E_AC_DECD; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip) { current[i].totalcost+=2+4; current[i].size+=3; current[i].rD=highval; current[i].action=E_AC_LDD_n; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; }
		}
		if (current[i].rL==lowval) {
			skip=0;
			if (current[i].rB==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rH=highval; current[i].action=E_AC_LDH_B; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rC==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rH=highval; current[i].action=E_AC_LDH_C; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rD==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rH=highval; current[i].action=E_AC_LDH_D; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rE==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rH=highval; current[i].action=E_AC_LDH_E; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rL==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rH=highval; current[i].action=E_AC_LDH_L; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rA==highval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rH=highval; current[i].action=E_AC_LDH_A; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && ((current[i].rH+1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rH=highval;current[i].action=E_AC_INCH; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip && ((current[i].rH-1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rH=highval;current[i].action=E_AC_DECH; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip) { current[i].totalcost+=2+4; current[i].size+=3; current[i].rH=highval; current[i].action=E_AC_LDH_n; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; }
		}
		// high placed
		if (current[i].rB==highval) {
			skip=0;
			if (current[i].rB==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rC=lowval; current[i].action=E_AC_LDC_B; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rD==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rC=lowval; current[i].action=E_AC_LDC_D; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rE==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rC=lowval; current[i].action=E_AC_LDC_E; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rH==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rC=lowval; current[i].action=E_AC_LDC_H; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rL==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rC=lowval; current[i].action=E_AC_LDC_L; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rA==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rC=lowval; current[i].action=E_AC_LDC_A; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && ((current[i].rC+1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rC=lowval;current[i].action=E_AC_INCC; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip && ((current[i].rC-1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rC=lowval;current[i].action=E_AC_DECC; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip) { current[i].totalcost+=2+4; current[i].size+=3; current[i].rC=lowval; current[i].action=E_AC_LDC_n; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; }
		}
		if (current[i].rD==highval) {
			skip=0;
			if (current[i].rB==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rE=lowval; current[i].action=E_AC_LDE_B; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rC==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rE=lowval; current[i].action=E_AC_LDE_C; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rD==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rE=lowval; current[i].action=E_AC_LDE_D; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rH==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rE=lowval; current[i].action=E_AC_LDE_H; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rL==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rE=lowval; current[i].action=E_AC_LDE_L; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rA==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rE=lowval; current[i].action=E_AC_LDE_A; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && ((current[i].rE+1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rE=lowval;current[i].action=E_AC_INCE; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip && ((current[i].rE-1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rE=lowval;current[i].action=E_AC_DECE; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip) { current[i].totalcost+=2+4; current[i].size+=3; current[i].rE=lowval; current[i].action=E_AC_LDE_n; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; }
		}
		if (current[i].rH==highval) {
			skip=0;
			if (current[i].rB==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rL=lowval; current[i].action=E_AC_LDL_B; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rC==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rL=lowval; current[i].action=E_AC_LDL_C; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rD==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rL=lowval; current[i].action=E_AC_LDL_D; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rE==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rL=lowval; current[i].action=E_AC_LDL_E; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rH==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rL=lowval; current[i].action=E_AC_LDL_H; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && current[i].rA==lowval) { current[i].totalcost+=1+4; current[i].size+=2; current[i].rL=lowval; current[i].action=E_AC_LDL_A; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;skip=1; }
			if (!skip && ((current[i].rL+1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rL=lowval;current[i].action=E_AC_INCL; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip && ((current[i].rL-1)&0xF)==lowval) { current[i].totalcost+=1+4;current[i].size+=2; current[i].rL=lowval;current[i].action=E_AC_DECL; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; skip=1; }
			if (!skip) { current[i].totalcost+=2+4; current[i].size+=3; current[i].rL=lowval; current[i].action=E_AC_LDL_n; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++; }
		}
		return;
	}

	skip=0;
	// is it possible to INC/DEC in order to match new value?
	if (current[i].rB>=0 && current[i].rC>=0) {
		if (((current[i].rB+1)&0xF)==highval && (((current[i].rC+1)&0xF)==lowval || ((current[i].rC-1)&0xF)==lowval)) {
			current[i].totalcost+=1;current[i].size+=1; current[i].rB=highval;current[i].action=E_AC_INCB; if (len>=2) BrutForce(&current[i],flux,len);
			skip=1;
		}
		if (((current[i].rB-1)&0xF)==highval && (((current[i].rC+1)&0xF)==lowval || ((current[i].rC-1)&0xF)==lowval)) {
			current[i].totalcost+=1;current[i].size+=1; current[i].rB=highval;current[i].action=E_AC_DECB; if (len>=2) BrutForce(&current[i],flux,len);
			skip=1;
		}
	}
	if (current[i].rD>=0 && current[i].rE>=0) {
		if (((current[i].rD+1)&0xF)==highval && (((current[i].rE+1)&0xF)==lowval || ((current[i].rE-1)&0xF)==lowval)) {
			current[i].totalcost+=1;current[i].size+=1; current[i].rD=highval;current[i].action=E_AC_INCD; if (len>=2) BrutForce(&current[i],flux,len);
			skip=1;
		}
		if (((current[i].rD-1)&0xF)==highval && (((current[i].rE+1)&0xF)==lowval || ((current[i].rE-1)&0xF)==lowval)) {
			current[i].totalcost+=1;current[i].size+=1; current[i].rD=highval;current[i].action=E_AC_DECD; if (len>=2) BrutForce(&current[i],flux,len);
			skip=1;
		}
	}
	if (current[i].rH>=0 && current[i].rL>=0) {
		if (((current[i].rH+1)&0xF)==highval && (((current[i].rL+1)&0xF)==lowval || ((current[i].rL-1)&0xF)==lowval)) {
			current[i].totalcost+=1;current[i].size+=1; current[i].rH=highval;current[i].action=E_AC_INCH; if (len>=2) BrutForce(&current[i],flux,len);
			skip=1;
		}
		if (((current[i].rH-1)&0xF)==highval && (((current[i].rL+1)&0xF)==lowval || ((current[i].rL-1)&0xF)==lowval)) {
			current[i].totalcost+=1;current[i].size+=1; current[i].rH=highval;current[i].action=E_AC_DECH; if (len>=2) BrutForce(&current[i],flux,len);
			skip=1;
		}
	}

	if (!skip) {
		// ok, nothing, start to fill something ^_^
		if (current[i].rB<0) {
			current[i].totalcost+=3; current[i].size+=3; current[i].rB=highval; current[i].rC=lowval; current[i].action=E_AC_LDBC; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;
			return;
		}
		if (current[i].rD<0) {
			current[i].totalcost+=3; current[i].size+=3; current[i].rD=highval; current[i].rE=lowval; current[i].action=E_AC_LDDE; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;
			return;
		}
		if (current[i].rH<0) {
			current[i].totalcost+=3; current[i].size+=3; current[i].rH=highval; current[i].rL=lowval; current[i].action=E_AC_LDHL; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;
			return;
		}
		// une fois que tout est chargé, on explore les possibilités
		current[i].totalcost+=3; current[i].size+=3; current[i].rB=highval; current[i].rC=lowval; current[i].action=E_AC_LDBC; current[i].preg=E_PUSH_BC; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;
		current[i].totalcost+=3; current[i].size+=3; current[i].rD=highval; current[i].rE=lowval; current[i].action=E_AC_LDDE; current[i].preg=E_PUSH_DE; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;
		current[i].totalcost+=3; current[i].size+=3; current[i].rH=highval; current[i].rL=lowval; current[i].action=E_AC_LDHL; current[i].preg=E_PUSH_HL; if (len>=2) BrutForce(&current[i],flux+2,len-2); i++;
	}
}
void _brut_MakeDiffStack(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux, int *codesize)
{
        #undef FUNC
        #define FUNC "MakeDiffStack"
	int i,j,k,l,first=1;
	int fluxlen=0,iflux=0;

	struct s_flux *sflux=NULL;
	unsigned char *flux=NULL;
	flux=malloc(longueur_flux);
	//sflux=calloc(65536,sizeof(struct s_flux));

	struct s_action_cache *head=NULL;
	struct s_action_cache *best=NULL;
	struct s_action_cache *current=NULL;
	struct s_action_cache *previous=NULL;

	char txt[256];
	char **reverse=NULL;
	int ireverse,mreverse;
	char *curtxt;

	// tree analysis
	int cpt=0,minTime;

	// reverse data
	for (i=0;i<longueur_flux;i+=2) {
		flux[i]=sp2[longueur_flux-2-i]; // valeur paire pour lowByte
		flux[i+1]=sp2[longueur_flux-1-i]; // valeur impaire pour le highByte
	}

longueur_flux=80;

	if (parameter->exhlde) diff_printf(output,lenoutput,"ex hl,de ; using this you must call the routine with sprite data MSB in D register\n");
	else diff_printf(output,lenoutput,"; this routine must be called with sprite data MSB in H register (from #40 to #4F)\n");
	

	head=calloc(E_AC_END,sizeof(struct s_action_cache)); // malloc+memset
	previous=head;
	for (j=0;j<E_AC_END;j++) {
		previous[j].action=E_AC_NOTHING;
		previous[j].preg=E_PUSH_END;
		previous[j].totalcost=0;
		previous[j].rA=previous[j].rB=previous[j].rC=previous[j].rD=previous[j].rE=previous[j].rH=previous[j].rL=-1;
		previous[j].next=NULL;
		previous[j].remaining=longueur_flux;
	}
	printf("brutForce\n");
	// BRUT FORCE THIS!
	BrutForce(previous,flux,longueur_flux);

	printf("analyseTree\n");
	// Analyse tree
	current=head;
	i=0;
	while (1) {
		if (current[i].next) {
			current=current[i].next;
			i=0;
		} else {
			if (!current->remaining) {
			//	printf("; totalcost=%d size=%d\n",current->totalcost,current->size);
				if (!best) best=current; else if (current->totalcost<best->totalcost) best=current; else if (current->totalcost==best->totalcost && current->size<best->size) best=current;
			}
			if (!current->previous) break;
			current=current->previous; // rollback
			i=1;
		}
	}
	if (current==head) printf("; cur=head\n");

	while (1) {
		switch (best->preg) {
			case E_PUSH_BC:strcpy(txt,"PUSH BC\n"); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_PUSH_DE:strcpy(txt,"PUSH DE\n"); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_PUSH_HL:strcpy(txt,"PUSH HL\n"); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			default:break;
		}
		switch (best->action) {
			case E_AC_NOTHING:break;
			case E_AC_LDBC:sprintf(txt,"LD BC,#%02X%02X : ",best->rB,best->rC); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDDE:sprintf(txt,"LD DE,#%02X%02X : ",best->rD,best->rE); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDHL:sprintf(txt,"LD HL,#%02X%02X : ",best->rH,best->rL); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_INCB:strcpy(txt,"INC B : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_INCC:strcpy(txt,"INC C : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_INCD:strcpy(txt,"INC D : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_INCE:strcpy(txt,"INC E : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_INCH:strcpy(txt,"INC H : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_INCL:strcpy(txt,"INC L : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDB_A:strcpy(txt,"LD B,A : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDC_A:strcpy(txt,"LD C,A : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDD_A:strcpy(txt,"LD D,A : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDE_A:strcpy(txt,"LD E,A : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDH_A:strcpy(txt,"LD H,A : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDL_A:strcpy(txt,"LD L,A : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDB_C:strcpy(txt,"LD B,C : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDB_D:strcpy(txt,"LD B,D : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDB_E:strcpy(txt,"LD B,E : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDB_H:strcpy(txt,"LD B,H : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDB_L:strcpy(txt,"LD B,L : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDC_B:strcpy(txt,"LD C,B : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDC_D:strcpy(txt,"LD C,D : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDC_E:strcpy(txt,"LD C,E : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDC_H:strcpy(txt,"LD C,H : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDC_L:strcpy(txt,"LD C,L : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDD_B:strcpy(txt,"LD D,B : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDD_C:strcpy(txt,"LD D,C : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDD_E:strcpy(txt,"LD D,E : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDD_H:strcpy(txt,"LD D,H : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDD_L:strcpy(txt,"LD D,L : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDE_B:strcpy(txt,"LD E,B : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDE_C:strcpy(txt,"LD E,C : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDE_D:strcpy(txt,"LD E,D : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDE_H:strcpy(txt,"LD E,H : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDE_L:strcpy(txt,"LD E,L : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDH_B:strcpy(txt,"LD H,B : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDH_C:strcpy(txt,"LD H,C : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDH_D:strcpy(txt,"LD H,D : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDH_E:strcpy(txt,"LD H,E : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDH_L:strcpy(txt,"LD H,L : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDL_B:strcpy(txt,"LD L,B : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDL_C:strcpy(txt,"LD L,C : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDL_D:strcpy(txt,"LD L,D : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDL_E:strcpy(txt,"LD L,E : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDL_H:strcpy(txt,"LD L,H : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDB_n:sprintf(txt,"LD B,%d : ",best->rB); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDC_n:sprintf(txt,"LD C,%d : ",best->rC); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDD_n:sprintf(txt,"LD D,%d : ",best->rD); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDE_n:sprintf(txt,"LD E,%d : ",best->rE); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDH_n:sprintf(txt,"LD H,%d : ",best->rH); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDL_n:sprintf(txt,"LD L,%d : ",best->rL); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_LDA_n:sprintf(txt,"LD A,%d : ",best->rA); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_DECB:strcpy(txt,"DEC B : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_DECC:strcpy(txt,"DEC C : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_DECD:strcpy(txt,"DEC D : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_DECE:strcpy(txt,"DEC E : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_DECH:strcpy(txt,"DEC H : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			case E_AC_DECL:strcpy(txt,"DEC L : "); curtxt=strdup(txt); ObjectArrayAddDynamicValueConcat((void **)&reverse,&ireverse,&mreverse,&curtxt,sizeof(char *)); break;
			default:printf("action foireuse %d\n",best->action);break;
		}
		if (!best->previous) break;
		best=best->previous; // rollback
	}
printf("; ireverse=%d\n",ireverse);
	for (i=ireverse-1;i>=0;i--) {
		printf("%s",reverse[i]); free(reverse[i]);
	}
	free(reverse);


	if (parameter->inch) diff_printf(output,lenoutput,"inc h\n");
	if (!parameter->noret) diff_printf(output,lenoutput,"ret\n");
	if (parameter->jpix) diff_printf(output,lenoutput,"jp (ix)\n");
	if (parameter->jpiy) diff_printf(output,lenoutput,"jp (iy)\n");
	if (parameter->jpmode) diff_printf(output,lenoutput,"jp %s\n",parameter->absolute);
	diff_printf(output,lenoutput,"\n");
	free(flux);
}

void MakeDiffStack(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux, int *codesize)
{
        #undef FUNC
        #define FUNC "MakeDiff"

	int i,j,first=1,idx=0;
	char txtbuffer[32];
	int current_reg[5]={-1,-1,-1,-1,-1};
	int fluxlen=0,iflux=0;
	int currentH=-1,currentL=-1;
	unsigned char vswap;

	unsigned char *flux=NULL;
	flux=malloc(longueur_flux);
	// reverse data
	j=longueur_flux-1;
	for (i=0;i<longueur_flux>>1;i++) {
		vswap=sp2[i];
		sp2[i]=sp2[j];
		sp2[j--]=vswap;
	}
	//
	currentH=flux[0]=sp2[longueur_flux-2]; // valeur paire pour lowByte INIT
	currentL=flux[1]=sp2[longueur_flux-1]; // valeur impaire pour le highByte INIT
	for (i=idx=2;i<longueur_flux;i+=2) {
		if (currentH!=sp2[longueur_flux-2-i] && sp2[longueur_flux-2-i]!=currentL) flux[idx++]=sp2[longueur_flux-2-i]; // valeur paire pour lowByte
		if (currentL!=sp2[longueur_flux-1-i]) flux[idx++]=sp2[longueur_flux-1-i]; // valeur impaire pour le highByte
	}

	if (parameter->exhlde) diff_printf(output,lenoutput,"ex hl,de ; using this you must call the routine with sprite data addr + #100 in DE register\n");
	else diff_printf(output,lenoutput,"; this routine must be called with HL containing the sprite data addr + #100\n");

	if (!parameter->noSPbackup) diff_printf(output,lenoutput,"ld (.backupSP+1),sp\n");
	diff_printf(output,lenoutput,"ld sp,hl\n");


	fluxlen=idx; // optimised flux

	for (i=0;i<longueur_flux;i+=2) {
		if (currentH==sp2[i]) {
			// H already at desired value
		} else if (sp2[i]==currentL) {
			diff_printf(output,lenoutput,"ld h,l : ");
			currentH=currentL;
		} else {
			ComputeDiffStats(output,lenoutput,flux,iflux,fluxlen,current_reg); iflux++;
			diff_printf(output,lenoutput,"ld h,%s\n",GetValStr(txtbuffer,current_reg,sp2[i]));
			currentH=sp2[i];
		}

		if (currentL==sp2[i+1]) {
			// L already at desired value
		} else {
			ComputeDiffStats(output,lenoutput,flux,iflux,fluxlen,current_reg); iflux++;
			diff_printf(output,lenoutput,"ld l,%s\n",GetValStr(txtbuffer,current_reg,sp2[i+1]));
			currentL=sp2[i+1];
		}
		diff_printf(output,lenoutput,"push hl\n");
	}
	if (!parameter->noSPbackup) diff_printf(output,lenoutput,".backupSP ld sp,#0000\n");

	if (!parameter->noret) diff_printf(output,lenoutput,"ret\n");
	if (parameter->jpix) diff_printf(output,lenoutput,"jp (ix)\n");
	if (parameter->jpiy) diff_printf(output,lenoutput,"jp (iy)\n");
	if (parameter->jpmode) diff_printf(output,lenoutput,"jp %s\n",parameter->absolute);
	diff_printf(output,lenoutput,"\n");
	free(flux);
}

void MakeDiff(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux, int *codesize)
{
        #undef FUNC
        #define FUNC "MakeDiff"

	int i,j,first=1;
	int previous_l_value=500;
	char txtbuffer[32];
	int current_reg[5]={-1,-1,-1,-1,-1};
	int fluxlen=0,iflux=0;
	int backLen;

	unsigned char *flux=NULL,*spback;
	if (parameter->unbalanced) {
		// pre-process sp1
		spback=sp1;
		sp1=malloc(longueur_flux);
		
		for (i=0;i<longueur_flux;i++) {
			int hasdiff=0;
			for (j=0;j<parameter->divint;j++) {
				if (spback[i+j*longueur_flux]!=sp2[i]) {
					hasdiff=1;
				}
			}
			if (hasdiff) sp1[i]=sp2[i]+1; else sp1[i]=sp2[i]; // rebuild sp1 according to multiple inputs
		}
	}

	flux=malloc(longueur_flux);
	for (i=0;i<longueur_flux;i++) if (sp1[i]!=sp2[i]) flux[fluxlen++]=sp2[i];

	*codesize=0;
	backLen=*lenoutput;

	if (parameter->exhlde) {
		diff_printf(output,lenoutput,"ex hl,de ; using this you must call the routine with sprite data MSB in D register\n");
	}
	else diff_printf(output,lenoutput,"; this routine must be called with sprite data MSB in H register (from #40 to #4F)\n");

	for (i=0;i<longueur_flux;i++) {
		/* traitement des metasprites */
		if ((i&0xFF)==0 && i!=0) {
			diff_printf(output,lenoutput,"inc h ; next sprite inside meta\n");
			*codesize=*codesize+1;
	diff_printf(output,lenoutput,"; b=%d c=%d d=%d e=%d a=%d\n",current_reg[0],current_reg[1],current_reg[2],current_reg[3],current_reg[4]);
		}

		//ComputeDiffStats(output,lenoutput,sp1,sp2,i,i==0?1:0,current_reg,fluxlen);

		if (sp1[i]!=sp2[i]) {
			ComputeDiffStats(output,lenoutput,flux,iflux,fluxlen,current_reg); iflux++;
			if (first) {
				/* first value may be after the first sprite! */
				diff_printf(output,lenoutput,"ld l,%s\n",GetValStr(txtbuffer,current_reg,i&0xFF));
				if (sp2[i]==i&0xF) {
					diff_printf(output,lenoutput,"ld (hl),l\n");
				} else {
					char shortval[32];
					strcpy(shortval,GetValStr(txtbuffer,current_reg,sp2[i]));
					if (shortval[0]!='#') {
						/* register cache */
						diff_printf(output,lenoutput,"ld (hl),%s\n",shortval);
					} else {
						/* try inc/dec optim */
						if (((sp1[i]+1)&0xF)==sp2[i]) diff_printf(output,lenoutput,"inc (hl)\n"); else
						if (((sp1[i]-1)&0xF)==sp2[i]) diff_printf(output,lenoutput,"dec (hl)\n"); else
						diff_printf(output,lenoutput,"ld (hl),%s\n",shortval);
					}
				}
				previous_l_value=i&0xFF;
				first=0;
			} else {
				/* optimised offset change */
	//printf("offset=%x sp2[i]=%x offset&0xF=%x\n",i,sp2[i],i&0xF);
				if (previous_l_value==i-1) diff_printf(output,lenoutput,"inc l : "); else
				if ((i&0xFF)==current_reg[0] && current_reg[0]>=0) diff_printf(output,lenoutput,"ld l,b : "); else
				if ((i&0xFF)==current_reg[1] && current_reg[1]>=0) diff_printf(output,lenoutput,"ld l,c : "); else
				if ((i&0xFF)==current_reg[2] && current_reg[2]>=0) diff_printf(output,lenoutput,"ld l,d : "); else
				if ((i&0xFF)==current_reg[3] && current_reg[3]>=0) diff_printf(output,lenoutput,"ld l,e : "); else
				if ((i&0xFF)==current_reg[4] && current_reg[4]>=0) diff_printf(output,lenoutput,"ld l,a : "); else diff_printf(output,lenoutput,"ld l,%d : ",i&0xFF);
				/* optimised value set (fixed v1.2) */
				if (sp2[i]==(i&0xF)) diff_printf(output,lenoutput,"ld (hl),l\n"); else diff_printf(output,lenoutput,"ld (hl),%s\n",GetValStr(txtbuffer,current_reg,sp2[i]));
				previous_l_value=i;
			}
		}
	}
	if (parameter->inch) diff_printf(output,lenoutput,"inc h\n");
	if (!parameter->noret) diff_printf(output,lenoutput,"ret\n");
	if (parameter->jpix) diff_printf(output,lenoutput,"jp (ix)\n");
	if (parameter->jpiy) diff_printf(output,lenoutput,"jp (iy)\n");
	if (parameter->jpmode) diff_printf(output,lenoutput,"jp %s\n",parameter->absolute);
	diff_printf(output,lenoutput,"\n");


	// compute codeSize
	if (parameter->autosplit) {
		int diz=backLen;
		int redo=1;
		int cs=0;
		char *loutput=*output;

		// ld (hl),
		// ld l,
		// inc ( / inc
		// dec (
		// ret
		// jp ( / jp
		// ex
		while (redo) {
			switch (loutput[diz]) {
				case 0:redo=0;break;
				case ' ':
				case '\n':diz++;
				case 'j': // jp
					 if (!strncmp(loutput+diz,"jp (ix)",7) || !strncmp(loutput+diz,"jp (iy)",7)) {
						 cs+=2;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else if (!strncmp(loutput+diz,"jp ",3)) {
						 cs+=3;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else {
						fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
						redo=0;
					 }
					 break;
				case 'e': // ex hl,de
					 if (!strncmp(loutput+diz,"ex hl,de",8)) {
						cs++;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else {
						fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
						redo=0;
					 }
					 break;
				case 'r': // ret
					 if (!strncmp(loutput+diz,"ret",3)) {
						cs++;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else {
						fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
						redo=0;
					 }
					 break;
				case 'd': // dec (hl)
					 if (!strncmp(loutput+diz,"dec (hl)",8)) {
						cs++;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else {
						fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
						redo=0;
					 }
					 break;
				case 'i': // inc ( / inc
					 if (!strncmp(loutput+diz,"inc (hl)",8)) {
						cs++;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else if (!strncmp(loutput+diz,"inc ",4)) {
						cs++;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else {
						fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
						redo=0;
					 }
					 break;
				case 'l': // ld ( / ld xx / ld x
					 if (!strncmp(loutput+diz,"ld (hl),",8)) {
						 diz+=8;
						 if (loutput[diz]>='a' && loutput[diz]<='l') cs++; else cs+=2;
						 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
					 } else if (!strncmp(loutput+diz,"ld ",3)) {
						 diz+=3;
						 if (loutput[diz+1]==',') { // 8bits
							diz+=2;
						 	if (loutput[diz]>='a' && loutput[diz]<='l') cs++; else cs+=2;
							 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
						 } else if (loutput[diz+2]==',') { // 16bits LD rr,nn
							cs+=3;
							 while (loutput[diz] && loutput[diz]!='\n' && loutput[diz]!=':') diz++;
						 } else {
							fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
							redo=0;
						 }

					 } else {
						fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
						redo=0;
					 }
					 break;
				default:fprintf(stderr,"internal error when disassembling for code size [%-30.30s]\n",loutput+diz);
			}
		}
		*codesize=cs;
	}

	free(flux);
	if (parameter->unbalanced) free(sp1);
}

int FileExists(char *filename) {
	FILE *f;
	f=fopen(filename,"rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

void addMask(unsigned char *mask, unsigned char *data, int len) {
	int i;
	for (i=0;i<len;i++) {
		mask[i]|=data[i];
	}
}

void Compiler(struct s_parameter *parameter)
{
	#undef FUNC
	#define FUNC "Compiler"

	/* fichiers */
	char filename1[2048],filename2[2048];
	unsigned char *data1,*data2,*mask;
	int i,j,ok=1,idx=0;
	int first=-1,last,iauto=0;
	FILE *fs;
	/* sequences */
	int zemax;
	/* multi-thread */
	struct s_compilation_action *compilation_actions=NULL;
	struct s_compilation_action compilation_action={0};
	int nbcaction=0,maxcaction=0;
	struct s_compilation_thread *ct;
	int nb_cores=0;
	/**/
	int lenoutput=0;
	char *output=NULL;

	compilation_action.uidx=parameter->label_unique_index_start;

	mask=malloc(256*parameter->meta);
	memset(mask,0,256*parameter->meta);

	if (0 && parameter->meta) {
		int len;

		// legacy but loud => will be better to change scripts => PERFORMANCES PHOQUE YEAH !!!

		fs=fopen(parameter->filename1,"rb");
		fseek(fs,0,SEEK_END);
		zemax=ftell(fs);
		fclose(fs);
		fprintf(stderr,"*** meta-sprite size=%d***\n",zemax);
		
		data1=malloc(zemax);
		FileReadBinary(parameter->filename1,data1,zemax);
		data2=malloc(zemax);
		FileReadBinary(parameter->filename2,data2,zemax);

		MakeDiff(&output,&lenoutput, parameter, data1, data2, zemax,NULL);

		len=lenoutput;
		while (len>1024) {
			printf("%.1024s",output+idx);
			len-=1024;
			idx+=1024;
		}
		if (len) printf("%.*s",len,output+idx);
		return;
	} else {
		if (parameter->nidx1) {
			if (!parameter->nidx2) {
				/* compile full sur un seul fichier */
				for (i=zemax=0;i<parameter->nidx1;i++) {
					if (parameter->idx1[i]>zemax) zemax=parameter->idx1[i];
				}
				/* on a l'index max, alors on peut calculer la taille max à lire => handling meta sprites */
				data1=malloc((zemax+1)*256*parameter->meta);
				FileReadBinary(parameter->filename1,data1,parameter->meta*256*(zemax+1));

				for (j=0;j<parameter->nidx1;j++) {
					/* copie du sprite courant depuis l'index idx1[j] */
				
					compilation_action.sp1=malloc(256*parameter->meta);
					compilation_action.sp2=malloc(256*parameter->meta);
					memcpy(compilation_action.sp2,data1+parameter->idx1[j]*256*parameter->meta,256*parameter->meta);
					for (i=0;i<256*parameter->meta;i++) {
						compilation_action.sp1[i]=compilation_action.sp2[i]+8;
					}
					compilation_action.idx=compilation_action.idx2=parameter->idx1[j];
					compilation_action.wrklen=256*parameter->meta;
					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
					compilation_action.uidx++;
				}
			} else {
				/* option multi possible ici */
				int divint;

				if (!parameter->unbalanced) {
					/* compile diff sur deux fichiers avec deux séquences! */
					if (parameter->nidx1!=parameter->nidx2) {
						fprintf(stderr,"sequences must have the same number of indexes\n");
						exit(-2);
					}
					divint=1;
				} else {
					parameter->divint=divint=parameter->nidx1/parameter->nidx2;
					if (divint<1) {
						fprintf(stderr,"first sequence must have more indexes (or equal) than second sequence (%d/%d)\n",parameter->nidx1,parameter->nidx2);
						exit(-2);
					}
				}
				for (i=zemax=0;i<parameter->nidx1;i++) {
					if (parameter->idx1[i]>zemax) zemax=parameter->idx1[i];
				}
				data1=malloc((zemax+1)*256*parameter->meta);
				FileReadBinary(parameter->filename1,data1,256*(zemax+1)*parameter->meta);
				for (i=zemax=0;i<parameter->nidx2;i++) {
					if (parameter->idx2[i]>zemax) zemax=parameter->idx2[i];
				}
				data2=malloc((zemax+1)*256*parameter->meta);
				FileReadBinary(parameter->filename2,data2,256*(zemax+1)*parameter->meta);

				if (parameter->unbalanced) {
					int u,us=0,usi;
					// unbalanced sequences
					for (j=0;j<parameter->nidx2;j++) {
						compilation_action.sp1=malloc(256*parameter->meta*divint);
						compilation_action.idx=parameter->idx1[us]; // enhance this part
						usi=0;
						for (u=us;u<us+divint;u++) {
							memcpy(compilation_action.sp1+256*parameter->meta*usi,data1+parameter->idx1[u]*256*parameter->meta,256*parameter->meta);
							usi++;
						}
						us+=divint;
						compilation_action.sp2=malloc(256*parameter->meta);
						memcpy(compilation_action.sp2,data2+parameter->idx2[j]*256*parameter->meta,256*parameter->meta);
						if (parameter->skipblank) {
							//addMask(mask,ompilation_action.sp1,256*parameter->meta);
							addMask(mask,compilation_action.sp2,256*parameter->meta);
							compilation_action.mask=mask;
						}
						compilation_action.idx2=parameter->idx2[j];
						compilation_action.wrklen=256*parameter->meta;
						ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
						compilation_action.uidx++;
					}
				} else {
					// legacy
					for (j=0;j<parameter->nidx1;j++) {
						compilation_action.sp1=malloc(256*parameter->meta);
						compilation_action.sp2=malloc(256*parameter->meta);
						memcpy(compilation_action.sp1,data1+parameter->idx1[j]*256*parameter->meta,256*parameter->meta);
						memcpy(compilation_action.sp2,data2+parameter->idx2[j]*256*parameter->meta,256*parameter->meta);
						if (parameter->skipblank) {
							//addMask(mask,ompilation_action.sp1,256*parameter->meta);
							addMask(mask,compilation_action.sp2,256*parameter->meta);
							compilation_action.mask=mask;
						}
						compilation_action.idx=parameter->idx1[j];
						compilation_action.idx2=parameter->idx2[j];
						compilation_action.wrklen=256*parameter->meta;
						ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
						compilation_action.uidx++;
					}
				}
			}
		} else {
			if (strstr(parameter->filename1,"%")) {
				fprintf(stderr,"automode with filenames\n");
				sprintf(filename1,parameter->filename1,0);
				if (!FileExists(filename1)) {
					sprintf(filename1,parameter->filename1,1);
					if (!FileExists(filename1)) {
						printf("sequence must start with a file 0 or 1 numbered!\n");
						exit(-1);
					} else {
						idx=first=1;
					}
				} else {
					idx=first=0;
				}
				iauto=1;
			}
			if (!iauto) {
				strcpy(filename1,parameter->filename1);
				if (parameter->filename2) strcpy(filename2,parameter->filename2); else strcpy(filename2,"");
			}

			while (ok) {
				if (iauto) {
					sprintf(filename1,parameter->filename1,idx);
					sprintf(filename2,parameter->filename1,idx+1);
					if (!FileExists(filename2)) {
						sprintf(filename2,parameter->filename1,first); /* reloop */
						ok=0;
					}
					if (parameter->compilediff) printf("; DIFF from %s to %s\n",filename1,filename2); else printf("; FULL from %s\n",filename1);
					if (parameter->label) {
						printf("%s%d:\n",parameter->label,idx);
					}
					idx++;
				} else {
					ok=0;
				}
				if (parameter->compilediff) {
					compilation_action.sp1=malloc(256);
					compilation_action.sp2=malloc(256);
					FileReadBinary(filename1,compilation_action.sp1,256);
					FileReadBinary(filename2,compilation_action.sp2,256);
					compilation_action.idx=idx-1;
					compilation_action.idx2=idx;
					compilation_action.wrklen=256;

					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
					compilation_action.uidx++;
				} else if (parameter->compilefull) {
					compilation_action.sp1=malloc(256);
					compilation_action.sp2=malloc(256);
					compilation_action.idx2=compilation_action.idx=idx-1;
					
					FileReadBinary(filename1,compilation_action.sp2,256);
					for (i=0;i<256;i++) {
						compilation_action.sp1[i]=compilation_action.sp2[i]+8;
					}
					compilation_action.wrklen=256;
					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
					compilation_action.uidx++;
				}
			}
		}
	}

	/* multi-thread execution */
	ct=SplitForThreads(nbcaction,compilation_actions,&nb_cores,parameter);
	ExecuteThreads(nb_cores,ct, MakeDiffThread);

	/* output assembly in legacy order */
	for (i=0;i<nb_cores;i++) {
		if (ct[i].output) {
			int len,idx=0;
			len=ct[i].lenoutput;
			while (len>1024) {
				printf("%.1024s",ct[i].output+idx);
				len-=1024;
				idx+=1024;
			}
			if (len) printf("%.*s",len,ct[i].output+idx);
		}
	}
				
				
}

/***************************************
	semi-generic body of program
***************************************/

/*
	Usage
	display the mandatory parameters
*/
void Usage()
{
	#undef FUNC
	#define FUNC "Usage"
	
	printf("%.*s.exe v3.1 / Edouard BERGE 2024-10\n",(int)(sizeof(__FILENAME__)-3),__FILENAME__);
	printf("\n");
	printf("syntaxe is: %.*s file1 [file2] [options]\n",(int)(sizeof(__FILENAME__)-3),__FILENAME__);
	printf("\n");
	printf("options:\n");
	printf("-idx <sequence(s)>  define sprite indexes to compute\n");
	printf("     sequence can be single values separated by comma\n");
	printf("      like 0,1,2,3,4 \n");
	printf("     or interval defined by values and dash\n");
	printf("      like 0-4  \n");
	printf("     you may mix indexes\n");
	printf("      like 1-63,0 ; from 1 to 63 then 0\n");
	printf("\n");
	printf("\n");
	printf("file processing\n");
	printf("-c         compile a full sprite\n");
	printf("-d         compile difference between two sprites or a sequence\n");
	printf("-m         compile difference with multi-sources (like -d with unbalanced sequences)\n");
	printf("-meta      compile consecutive sprites\n");
	printf("-stack     enable stack generator (default is interrupt safe)\n");
	printf("-skipblank skip all time blank pixels\n");
	printf("\n");
	printf("code generation global options:\n");
	printf("-exhlde    add a EX HL,DE          at the beginning of the routine\n");
	printf("-noret     do not add RET at the end of the routine\n");
	printf("-jpix      add a JP (IX)  at the end of the routine\n");
	printf("-jpiy      add a JP (IY)  at the end of the routine\n");
	printf("-jp <nn>   add a JP <label or value> at the end of the routine\n");
	printf("\n");
	printf("interrupt safe code generation:\n");
	printf("-inch      add a INC H    at the end of the routine\n");
	printf("           default return uses RET\n");
	printf("\n");
	printf("stack code generation:\n");
	printf("-noSPbackup\n");
	printf("\n");
	printf("label generation:\n");
	printf("-l <label> insert a numbered label at the beginning (for multi diff)\n");
	printf("-lidx use a unique counter at the end of the function label\n");
	printf("-lstartidx <value> set the starting value for the counter (default:0)\n");
	printf("\n");
	printf("-autosplit bank/rombank\n");
	
	exit(-1);
}

void GetSequence(struct s_parameter *parameter, char *sequence)
{
	int *idx=NULL;
	int nidx=0,midx=0;

	int i=0,j,valstar,valend;

	while (sequence[i]) {
		/* après un intervale on doit passer à la suite */
		if (sequence[i]==',') i++;
		valstar=0;
		while (sequence[i]>='0' && sequence[i]<='9') {
			valstar=valstar*10+sequence[i]-'0';
			i++;
		}
		switch (sequence[i]) {
			case ',':
				i++;
			case 0:
			default:valend=valstar;break;
			case '-':
				i++;
				valend=0;
				while (sequence[i]>='0' && sequence[i]<='9') {
					valend=valend*10+sequence[i]-'0';
					i++;
				}
		}
//printf("vs=%d ve=%d\n",valstar,valend);
		if (valstar<=valend) {
			for (j=valstar;j<=valend;j++) IntArrayAddDynamicValueConcat(&idx,&nidx,&midx,j);
		} else {
			for (j=valstar;j>=valend;j--) IntArrayAddDynamicValueConcat(&idx,&nidx,&midx,j);
		}
	}

//for (i=0;i<nidx;i++) printf("%d ",idx[i]);
//printf("\n");

	if (!parameter->idx1) {
		parameter->idx1=idx;
		parameter->nidx1=nidx;
		parameter->midx1=midx;
	} else if (!parameter->idx2) {
		parameter->idx2=idx;
		parameter->nidx2=nidx;
		parameter->midx2=midx;
	} else {
		fprintf(stderr,"only 2 sequences max to define!!\n");
		Usage();
	}
}

/*
	ParseOptions
	
	used to parse command line and configuration file
*/
int ParseOptions(char **argv,int argc, struct s_parameter *parameter)
{
	#undef FUNC
	#define FUNC "ParseOptions"

	int i=0;

	if (strcmp(argv[i],"-inch")==0) {
		parameter->inch=1;
	} else if (strcmp(argv[i],"-skipblank")==0) {
		parameter->skipblank=1;
	} else if (strcmp(argv[i],"-stack")==0) {
		parameter->stack=1;
	} else if (strcmp(argv[i],"-noSPbackup")==0) {
		parameter->noSPbackup=1;
	} else if (strcmp(argv[i],"-lidx")==0) {
		parameter->label_unique_index=1;
	} else if (strcmp(argv[i],"-lstartidx")==0) {
		if (i+1<argc) {
			parameter->label_unique_index_start=atoi(argv[++i]);
		} else Usage();
	} else if (strcmp(argv[i],"-idx")==0) {
		if (i+1<argc) {
			GetSequence(parameter,argv[++i]);
		} else Usage();
	} else if (strcmp(argv[i],"-meta")==0) {
		if (i+1<argc) {
			parameter->meta=atoi(argv[++i]);
			if (parameter->meta<2) {
				fprintf(stderr,"************************************************\n");
				fprintf(stderr,"usage is : -meta <size>           with size>1\n");
				fprintf(stderr,"************************************************\n");
				Usage();
			}
		} else {
			fprintf(stderr,"************************************************\n");
			fprintf(stderr,"   -meta without parameter is deprecated!!!\n");
			fprintf(stderr,"************************************************\n");
			Usage();
		}
	} else if (strcmp(argv[i],"-jp")==0) {
		parameter->noret=1;
		parameter->jpmode=1;
		if (i+1<argc) {
			parameter->absolute=argv[++i];
		} else Usage();
	} else if (strcmp(argv[i],"-autosplit")==0) {
		parameter->autosplit=1;
	} else if (strcmp(argv[i],"-noret")==0) {
		parameter->noret=1;
	} else if (strcmp(argv[i],"-exhlde")==0) {
		parameter->exhlde=1;
	} else if (strcmp(argv[i],"-jpix")==0) {
		parameter->noret=1;
		parameter->jpix=1;
	} else if (strcmp(argv[i],"-jpiy")==0) {
		parameter->noret=1;
		parameter->jpiy=1;
	} else if (argv[i][0]=='-') {
		switch(argv[i][1])
		{
			case 'L':
			case 'l':
				if (i+1<argc) {
					parameter->label=argv[++i];
				} else Usage();
				break;
			case 'C':
			case 'c':
				parameter->compilefull=1;
				break;
			case 'M':
			case 'm':
				parameter->unbalanced=1;
			case 'D':
			case 'd':
				parameter->compilediff=1;
				break;
			case 'H':
			case 'h':Usage();
			default:
				Usage();				
		}
	} else {
		if (!parameter->filename1) parameter->filename1=argv[i]; else
		if (!parameter->filename2) parameter->filename2=argv[i]; else
			Usage();
	}
	return i;
}

/*
	GetParametersFromCommandLine	
	retrieve parameters from command line and fill pointers to file names
*/
void GetParametersFromCommandLine(int argc, char **argv, struct s_parameter *parameter)
{
	#undef FUNC
	#define FUNC "GetParametersFromCommandLine"
	int i;
	
	for (i=1;i<argc;i++)
		i+=ParseOptions(&argv[i],argc-i,parameter);

	if (parameter->jpix && parameter->jpiy) {
		fprintf(stderr,"options -jpix and -jpiy are exclusive\n");
		Usage();
	}
	if ((parameter->jpix || parameter->jpiy || parameter->jpmode) && !parameter->noret) {
		fprintf(stderr,"; disabling RET at the end of the routine\n");
		parameter->noret=1;
	}

	if (parameter->unbalanced) {
		int divint;
		// multidiff need 2 sequences
		if (!parameter->nidx1 || !parameter->nidx2) {
			fprintf(stderr,"unbalanced diff need 2 sequences\n");
			Usage();
		}
		divint=parameter->nidx1/parameter->nidx2;

		if (parameter->nidx1==divint*parameter->nidx2) {

		} else {
			fprintf(stderr,"unbalanced usage must match sequences seq1 != seq2 x %d\n",divint);
			Usage();
		}
	}
	if (parameter->meta<2) parameter->meta=1;

	if (parameter->compilefull && parameter->compilediff) {
		fprintf(stderr,"options -c and -d are exclusive\n");
		Usage();
	}
	if (!parameter->compilefull && !parameter->compilediff) {
		fprintf(stderr,"use any compilation option like -c -d -m\n");
		Usage();
	}
}

/*
	main
	
	check parameters
	execute the main processing
*/
void main(int argc, char **argv)
{
	#undef FUNC
	#define FUNC "main"

	struct s_parameter parameter={0};

	GetParametersFromCommandLine(argc,argv,&parameter);
	Compiler(&parameter);
	exit(0);
}



