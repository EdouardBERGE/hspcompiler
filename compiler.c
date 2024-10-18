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


struct s_parameter {
	char *filename1;
	char *filename2;
	char *label;
	int label_unique_index;
	int label_unique_index_start;
	int compilefull;
	int compilediff;
	int inch;
	int noret;
	int meta;
	int jpix, jpiy, exhlde, jpmode;
	char *absolute;

	int *idx1;
	int nidx1,midx1;
	int *idx2;
	int nidx2,midx2;
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
	int idx,idx2;
	int wrklen;
	int uidx;
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

void MakeDiff(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux);

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
		MakeDiff(&ct->output, &ct->lenoutput,ct->parameter,ct->action[i].sp1,ct->action[i].sp2,ct->action[i].wrklen);
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

struct s_flux {
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
	if (strcmp(reg[rmax],"a")==0 && !flux[start]) { // optimisation propre à A
		diff_printf(output,lenoutput,"\n/* stat update */ xor a ; idx=%d\n",start);
	} else {
		rvalue=flux[start];
		ovalue=cval[rmax];

		if (((rvalue+1)&0xFF)==ovalue) diff_printf(output,lenoutput,"\n/* stat update */ dec %s ; idx=%d\n",reg[rmax],start); else
		if (((rvalue-1)&0xFF)==ovalue) diff_printf(output,lenoutput,"\n/* stat update */ inc %s ; idx=%d\n",reg[rmax],start); else
						diff_printf(output,lenoutput,"\n/* stat update */ ld %s,#%02X ; idx=%d\n",reg[rmax],flux[start],start);
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
                        if (fbefor[flux[i]].cpt>4) {
                                // init!
                                cval[mreg++]=flux[i];
                                fbefor[flux[i]].cpt=-size-1; // annuler les prochains tests
                                if (mreg==NBREG) break;
                        }
                }
                // reinit for 2nd pass
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



void MakeDiff(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux)
{
        #undef FUNC
        #define FUNC "MakeDiff"

	int i,j,first=1;
	int previous_l_value=500;
	char txtbuffer[32];
	int current_reg[5]={-1,-1,-1,-1,-1};
	int fluxlen=0,iflux=0;

	unsigned char *flux=NULL;
	flux=malloc(longueur_flux);
	for (i=0;i<longueur_flux;i++) if (sp1[i]!=sp2[i]) flux[fluxlen++]=sp2[i];

	if (parameter->exhlde) diff_printf(output,lenoutput,"ex hl,de ; using this you must call the routine with sprite data MSB in D register\n");
	else diff_printf(output,lenoutput,"; this routine must be called with sprite data MSB in H register (from #40 to #4F)\n");

	for (i=0;i<longueur_flux;i++) {
		/* traitement des metasprites */
		if ((i&0xFF)==0 && i!=0) {
			diff_printf(output,lenoutput,"inc h ; next sprite inside meta\n");
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
}

int FileExists(char *filename) {
	FILE *f;
	f=fopen(filename,"rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

void Compiler(struct s_parameter *parameter)
{
	#undef FUNC
	#define FUNC "Compiler"

	/* fichiers */
	char filename1[2048],filename2[2048];
	unsigned char *data1,*data2;
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

		MakeDiff(&output,&lenoutput, parameter, data1, data2, zemax);

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
				/* compile diff sur deux fichiers avec deux séquences! */
				if (parameter->nidx1!=parameter->nidx2) {
					fprintf(stderr,"pour compiler en diff deux séquences il faut deux séquences avec le même nombre d'indexes\n");
					exit(-2);
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

				for (j=0;j<parameter->nidx1;j++) {
					compilation_action.sp1=malloc(256*parameter->meta);
					compilation_action.sp2=malloc(256*parameter->meta);
					memcpy(compilation_action.sp1,data1+parameter->idx1[j]*256*parameter->meta,256*parameter->meta);
					memcpy(compilation_action.sp2,data2+parameter->idx2[j]*256*parameter->meta,256*parameter->meta);
					compilation_action.idx=parameter->idx1[j];
					compilation_action.idx2=parameter->idx2[j];
					compilation_action.wrklen=256*parameter->meta;
					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
					compilation_action.uidx++;
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
	
	printf("%.*s.exe v2.0 / Edouard BERGE 2024-10\n",(int)(sizeof(__FILENAME__)-3),__FILENAME__);
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
	printf("-meta      compile consecutive sprites\n");
	printf("\n");
	printf("code generation:\n");
	printf("-exhlde    add a EX HL,DE          at the beginning of the routine\n");
	printf("-inch      add a INC H    at the end of the routine\n");
	printf("-noret     do not add RET at the end of the routine\n");
	printf("-jpix      add a JP (IX)  at the end of the routine\n");
	printf("-jpiy      add a JP (IY)  at the end of the routine\n");
	printf("-jp <nn>   add a JP <label or value> at the end of the routine\n");
	printf("label generation:\n");
	printf("-l <label> insert a numbered label at the beginning (for multi diff)\n");
	printf("-lidx use a unique counter at the end of the function label\n");
	printf("-lstartidx <value> set the starting value for the counter (default:0)\n");
	printf("\n");
	
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
		parameter->jpmode=1;
		if (i+1<argc) {
			parameter->absolute=argv[++i];
		} else Usage();
	} else if (strcmp(argv[i],"-noret")==0) {
		parameter->noret=1;
	} else if (strcmp(argv[i],"-exhlde")==0) {
		parameter->exhlde=1;
	} else if (strcmp(argv[i],"-jpix")==0) {
		parameter->jpix=1;
	} else if (strcmp(argv[i],"-jpiy")==0) {
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
		printf("options -jpix and -jpiy are exclusive\n");
		exit(-1);
	}
	if ((parameter->jpix || parameter->jpiy || parameter->jpmode) && !parameter->noret) {
		fprintf(stderr,"; disabling RET at the end of the routine\n");
		parameter->noret=1;
	}

	if (parameter->meta<2) parameter->meta=1;

	if (parameter->compilefull && parameter->compilediff) {
		printf("options -c and -d are exclusive\n");
		exit(-1);
	}
	if (!parameter->compilefull && !parameter->compilediff) {
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



