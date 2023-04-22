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
	int compilefull;
	int compilediff;
	int inch;
	int noret;
	int meta;
	int metasize;
	int jpix, jpiy, exhlde,jpmode;
	int animate;

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
	int lng;
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
		for (j=0;j<ct->action[i].lng;j++) {
			ct->action[i].sp1[j]&=0xF;
			ct->action[i].sp2[j]&=0xF;
		}
		if (ct->parameter->label) {
			if (ct->action[i].idx==ct->action[i].idx2) diff_printf(&ct->output,&ct->lenoutput,"%s%d:\n",ct->parameter->label,ct->action[i].idx);
				else diff_printf(&ct->output,&ct->lenoutput,"%s%d_%d:\n",ct->parameter->label,ct->action[i].idx,ct->action[i].idx2);
		}
		MakeDiff(&ct->output, &ct->lenoutput,ct->parameter,ct->action[i].sp1,ct->action[i].sp2,ct->action[i].lng);
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

char *GetValStr(char *txtbuffer, int *current_reg,int v) {
        #undef FUNC
        #define FUNC "GetValStr"

	static char *txtreg[5]={"a","b","c","d","e"};
	int i;

	for (i=0;i<5;i++) {
		if (v==current_reg[i]) {
			sprintf(txtbuffer,"%s",txtreg[i]);
			return txtbuffer;
		}
	}
	sprintf(txtbuffer,"#%X",v);
	return txtbuffer;
}
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
	/* du plus grand au plus petit */
	return sb->cpt-sa->cpt;
}
int *ComputeDiffStats(char **output, int *lenoutput,unsigned char *sp1, unsigned char *sp2, int start, int first, int *current_reg, int longueur_flux)
{
	#undef FUNC
	#define FUNC "ComputeDiffState"

	static char *txtreg[5]={"a","b","c","d","e"};

	static int valregA=-1,valregB=-1,valregC=-1,valregD=-1,valregE=-1;
	struct s_regupdate regupdate[5];
	struct s_sprval switchspr;
	struct s_sprval diff[16];
	struct s_sprval shortdiff[16];
	int old_reg[5];
	int newreg[5];
	int ireg=0;
	int i,j,k,l;

	int kk,change=0;

#define STAT_INFO 0

	/******************************************************
	      i n i t i a l i s e    s t a t s
	******************************************************/
	if (first) {
                // reset
                valregA=valregB=valregC=valregD=valregE=-1;
	}
	/* backup reg */
	for (i=0;i<5;i++) old_reg[i]=current_reg[i];
	
	/* get and sort values */
	for (i=0;i<16;i++) {
		diff[i].val=i;
		diff[i].cpt=0;
		diff[i].prv=0;
	}
	for (i=start;i<longueur_flux;i++) {
		if (sp1[i]!=sp2[i] && sp2[i]!=(i&0xF)) diff[sp2[i]].cpt++;
	}
	/* where are the previous values in the stats? */
	for (i=0;i<5;i++) {
		for (j=0;j<16;j++) {
			if (diff[j].val==old_reg[i]) {
				diff[j].prv=1;
				break;
			}
		}
	}
	qsort(diff,16,sizeof(struct s_sprval),compare_sprval);

#if STAT_INFO
	printf("**** before optim - start=%d\n",start);
	for (i=0;i<16;i++) {
		if (!diff[i].cpt) break;
		printf("diff[%d] val=%d cpt=%d prv=%d\n",i,diff[i].val,diff[i].cpt,diff[i].prv);
	}
#endif
	/******************************************************
	      r e o p t i m i s e    s t a t s
	******************************************************/
	/* especially at the beginning of the sprite we need to look forward for a better optimisation*/

/* pas besoin de chercher une autre optim si il y a peu de couleurs au total!!!! @@TODO */

	if (diff[4].cpt>3) {
#if STAT_INFO		
		printf("-> try optimisation\n");
#endif
		/* can we change a secondary color before the use of a major color? */
		for (j=0;j<5;j++) {
			for (i=start;i<longueur_flux;i++) {
				if (sp1[i]!=sp2[i] && sp2[i]!=(i&0xF) && sp2[i]==diff[j].val) {
					break;
				}
			}
			/* do stats for changes until the marker i */
			for (l=0;l<16;l++) {
				shortdiff[l].val=diff[l].val;
				shortdiff[l].cpt=0;
				shortdiff[l].prv=diff[l].prv;
			}
			/* stats for secondary colors only */
			for (l=start;l<i;l++) {
				for (k=5;k<16;k++) {
					if (sp1[l]!=sp2[l] && sp2[l]!=(l&0xF) && sp2[l]==shortdiff[k].val) shortdiff[k].cpt++;
				}
			}
			qsort(shortdiff,16,sizeof(struct s_sprval),compare_sprval);
#if STAT_INFO
			printf("stats de %d a %d pour la couleur %d\n",start,i,diff[j].val);
			for (l=0;l<5;l++) {
				if (!shortdiff[l].cpt) break;
				printf("shortdiff[%d] val=%d cpt=%d\n",l,shortdiff[l].val,shortdiff[l].cpt);
			}
#endif
#if 0
			/***** code deprecated by v1.3 */
			/* apply optimisation if possible */
			if (shortdiff[0].prv && shortdiff[0].cpt) {
				/* toujours reprendre les surcharges commencées */
				for (k=0;diff[k].val!=shortdiff[0].val;k++);
				switchspr=diff[j];
				diff[j]=diff[k];
				diff[k]=switchspr;
#endif
			/* is it an outsider? v1.3 */
			if (shortdiff[0].prv) {
				int treshold;

				/* si on peut optimiser un registre alors le treshold est plus faible */
				if (((shortdiff[0].val+1)&15)==diff[j].val || ((shortdiff[0].val-1)&15)==diff[j].val) treshold=2; else treshold=4;
				/* on conditionne le takeover à la valeur qui arrive de suite! */
				if (shortdiff[0].cpt>=treshold && (shortdiff[0].val==sp2[start] || !start)) {
                                        /* takeover on bigger count */
					for (k=0;diff[k].val!=shortdiff[0].val;k++);
					switchspr=diff[j];
					diff[j]=diff[k];
					diff[k]=switchspr;
				}

#if STAT_INFO
printf("surcharge possible de val=%d cpt=%d\n",shortdiff[0].val,shortdiff[0].cpt);
printf("couleur à surcharger %d par %d\n",diff[j].val,diff[k].val);
#endif
#if 0
			/***** code deprecated by v1.3 */
			} else if (shortdiff[0].cpt<5) {
				/* si on n'a rien au dessus de 5 inutile de continuer */
				break;
#endif
			} else {
				/* confirm choice by switching values */
				for (k=0;diff[k].val!=shortdiff[0].val;k++);
				switchspr=diff[j];
				diff[j]=diff[k];
				diff[k]=switchspr;
#if STAT_INFO
printf("surcharge possible de val=%d cpt=%d\n",shortdiff[0].val,shortdiff[0].cpt);
printf("couleur à surcharger %d par %d\n",diff[j].val,diff[k].val);
#endif
			}
		}
	}

#if STAT_INFO
printf("**** after optim\n");
	for (i=0;i<16;i++) {
		if (!diff[i].cpt) break;
		printf("diff[%d] val=%d cpt=%d prv=%d\n",i,diff[i].val,diff[i].cpt,diff[i].prv);
	}
#endif

	/******************************************************
	      i n i t i a l i s e r
	******************************************************/
	if (first) {
		for (i=0;i<5;i++) {
			//printf("diff[%d].cpt=%d val=%d\n",i,diff[i].cpt,diff[i].val);
			if (diff[i].cpt<3) {
				current_reg[i]=diff[i].val=-1;
			} else {
				current_reg[i]=diff[i].val;
			}
		}
		if (diff[0].val==0) {
			diff_printf(output,lenoutput,"xor a\n");
			valregA=diff[0].val;
		} else if (diff[0].val>0) {
			diff_printf(output,lenoutput,"ld a,%d\n",diff[0].val);
			valregA=diff[0].val;
		}
		if (diff[1].val>=0 && diff[2].val>=0) diff_printf(output,lenoutput,"ld bc,#%X\n",diff[1].val*256+diff[2].val); else
		{
			if (diff[1].val>=0) diff_printf(output,lenoutput,"ld b,%d\n",diff[1].val); else
			if (diff[2].val>=0) diff_printf(output,lenoutput,"ld c,%d\n",diff[2].val);
                        valregB=diff[1].val;
                        valregC=diff[2].val;
		}
		if (diff[3].val>=0 && diff[4].val>=0) diff_printf(output,lenoutput,"ld de,#%X\n",diff[3].val*256+diff[4].val); else
		{
			if (diff[3].val>=0) diff_printf(output,lenoutput,"ld d,%d\n",diff[3].val);
			if (diff[4].val>=0) diff_printf(output,lenoutput,"ld e,%d\n",diff[4].val);
                        valregD=diff[3].val;
                        valregE=diff[4].val;
		}
		//diff_printf(output,lenoutput,"\n");
		
		/* améliorer l'init en cas de seulement deux registres... A+B -> BC */
		
		return current_reg;
	}
	/******************************************************
	      b u f f e r e d    u p d a t e r
	******************************************************/
	/* same values as previous set? */
	for (i=0;i<5;i++) {
		/* new top value not found in previous set */
		if (!diff[i].prv) {
			/* override a prv with stat-3 */
#if 0
			for (j=15;j>4;j--) {
				/* look from the end of stats */
				if (diff[j].prv && diff[i].cpt-3>diff[j].cpt) {
#else
			/***********************************************/
			/********** patch 1.3 **************************/
			/***********************************************/
			for (j=15;j>=0;j--) {
				/* look from the end of stats v1.3 */
				if (diff[j].prv && diff[i].cpt-3>diff[j].cpt && (diff[i].val==sp2[start] || !start)) {
#endif
					/* need to know which register will be replaced */
					for (k=0;k<5;k++) {
						if (current_reg[k]==diff[j].val) break;
					}
					if (k==5) {
						fprintf(stderr,"bug dans la reaffectation des registres\n");
						exit(-1);
					}

					/* register change is buffered */
					regupdate[ireg].reg=k;
					regupdate[ireg].val=diff[i].val;
					regupdate[ireg].oldval=diff[j].val;
					ireg++;
					/* confirm register change */
					current_reg[k]=diff[i].val;
					diff[j].prv=0;
					diff[i].prv=1;
					break;
				}
			}
		}
	}
	/******************************************************
	      u p d a t e
	******************************************************/
	if (ireg) {
		int ia=-1,ib=-1,ic=-1,id=-1,ie=-1;

		diff_printf(output,lenoutput,"; stats update\n");
//fprintf(stderr,"update\n");		
		/* get A,B,C,D,E indexes */
		for (i=0;i<ireg;i++) {
			switch (regupdate[i].reg) {
				case 0:
					/* optimised A v1.4 */
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
				case 1:ib=i;break;
				case 2:ic=i;break;
				case 3:id=i;break;
				case 4:ie=i;break;
				default:diff_printf(output,lenoutput,"register error in update\n");exit(-1);
			}
		}
		/* try to pack BC */
		if (ib>=0 && ic>=0) {
			int packed=1;
			if ((regupdate[ib].val==(regupdate[ib].oldval-1)&0xF) || (regupdate[ib].val==(regupdate[ib].oldval+1)&0xF)) {
				if ((regupdate[ic].val==(regupdate[ic].oldval-1)&0xF) || (regupdate[ic].val==(regupdate[ic].oldval+1)&0xF)) {
					/* do not pack BC for thoses values 1.2 */
					if (regupdate[ib].val==(regupdate[ib].oldval-1)&0xF) diff_printf(output,lenoutput,"dec b :"); else diff_printf(output,lenoutput,"inc b :");
					if (regupdate[ic].val==(regupdate[ic].oldval-1)&0xF) diff_printf(output,lenoutput,"dec c\n"); else diff_printf(output,lenoutput,"inc c\n");
					packed=0;
				}
			}
			if (packed) diff_printf(output,lenoutput,"ld bc,#%X\n",regupdate[ib].val*256+regupdate[ic].val);
			valregB=regupdate[ib].val;
                        valregC=regupdate[ic].val;
		} else {
			if (ib>=0) {
				if (regupdate[ib].val==(regupdate[ib].oldval-1)&0xF) diff_printf(output,lenoutput,"dec b\n"); else
				if (regupdate[ib].val==(regupdate[ib].oldval+1)&0xF) diff_printf(output,lenoutput,"inc b\n"); else diff_printf(output,lenoutput,"ld b,%d\n",regupdate[ib].val);
				valregB=regupdate[ib].val;
			}
			if (ic>=0) {
				if (regupdate[ic].val==(regupdate[ic].oldval-1)&0xF) diff_printf(output,lenoutput,"dec c\n"); else
				if (regupdate[ic].val==(regupdate[ic].oldval+1)&0xF) diff_printf(output,lenoutput,"inc c\n"); else diff_printf(output,lenoutput,"ld c,%d\n",regupdate[ic].val);
                        	valregC=regupdate[ic].val;
			}
		}
		/* try to pack DE */
		if (id>=0 && ie>=0) {
			/* (fixed v1.2) */
			int packed=1;
			if ((regupdate[id].val==(regupdate[id].oldval-1)&0xF) || (regupdate[id].val==(regupdate[id].oldval+1)&0xF)) {
				if ((regupdate[ie].val==(regupdate[ie].oldval-1)&0xF) || (regupdate[ie].val==(regupdate[ie].oldval+1)&0xF)) {
					/* do not pack DE for thoses values 1.2 */
					if (regupdate[id].val==(regupdate[id].oldval-1)&0xF) diff_printf(output,lenoutput,"dec d :"); else diff_printf(output,lenoutput,"inc d :");
					if (regupdate[ie].val==(regupdate[ie].oldval-1)&0xF) diff_printf(output,lenoutput,"dec e\n"); else diff_printf(output,lenoutput,"inc e\n");
					packed=0;
				}
			}
			if (packed) diff_printf(output,lenoutput,"ld de,#%X\n",regupdate[id].val*256+regupdate[ie].val);
                        valregD=regupdate[id].val;
                        valregE=regupdate[ie].val;
		} else {
			if (id>=0) {
				if (regupdate[id].val==(regupdate[id].oldval-1)&0xF) diff_printf(output,lenoutput,"dec d\n"); else
				if (regupdate[id].val==(regupdate[id].oldval+1)&0xF) diff_printf(output,lenoutput,"inc d\n"); else diff_printf(output,lenoutput,"ld d,%d\n",regupdate[id].val);
	                        valregD=regupdate[id].val;
			}
			if (ie>=0) {
				if (regupdate[ie].val==(regupdate[ie].oldval-1)&0xF) diff_printf(output,lenoutput,"dec e\n"); else
				if (regupdate[ie].val==(regupdate[ie].oldval+1)&0xF) diff_printf(output,lenoutput,"inc e\n"); else diff_printf(output,lenoutput,"ld e,%d\n",regupdate[ie].val);
                        	valregE=regupdate[ie].val;
			}
		}
	}
	return current_reg;
}



void MakeDiff(char **output, int *lenoutput, struct s_parameter *parameter, unsigned char *sp1, unsigned char *sp2, int longueur_flux)
{
        #undef FUNC
        #define FUNC "MakeDiff"

	int i,j,first=1;
	int previous_l_value=500;
	char txtbuffer[32];
	int current_reg[5]={-1,-1,-1,-1,-1};

	if (parameter->exhlde) diff_printf(output,lenoutput,"ex hl,de ; using this you must call the routine with sprite data MSB in D register\n");
	else diff_printf(output,lenoutput,"; this routine must be called with sprite data MSB in H register (from #40 to #4F)\n");

	if (parameter->animate) {
		diff_printf(output,lenoutput,".routine{cpt} ld h,HSPADR\n");
	}

	for (i=0;i<longueur_flux;i++) {
		/* traitement des metasprites */
		if ((i&0xFF)==0 && i!=0) {
			diff_printf(output,lenoutput,"inc h ; next sprite inside meta\n");
		}

		ComputeDiffStats(output,lenoutput,sp1,sp2,i,i==0?1:0,current_reg,longueur_flux);
		if (sp1[i]!=sp2[i]) {
			if (first) {
				/* first value may be after the first sprite! */
				diff_printf(output,lenoutput,"ld l,%s\n",GetValStr(txtbuffer,current_reg,i&0xFF));
				if (sp2[i]==i&0xF) diff_printf(output,lenoutput,"ld (hl),l\n");
				else {
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
				if ((i&0xFF)==current_reg[0]) diff_printf(output,lenoutput,"ld l,a : "); else
				if ((i&0xFF)==current_reg[1]) diff_printf(output,lenoutput,"ld l,b : "); else
				if ((i&0xFF)==current_reg[2]) diff_printf(output,lenoutput,"ld l,c : "); else
				if ((i&0xFF)==current_reg[3]) diff_printf(output,lenoutput,"ld l,d : "); else
				if ((i&0xFF)==current_reg[4]) diff_printf(output,lenoutput,"ld l,e : "); else diff_printf(output,lenoutput,"ld l,%d : ",i&0xFF);
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

	if (parameter->animate) {
		diff_printf(output,lenoutput,"cpt+=1\n");
	}
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

	// compilation de meta sprite en mode simple
	if (parameter->meta && !parameter->metasize) {
		int len;

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
		int spritesize=256;

		if (parameter->meta && parameter->metasize) spritesize=parameter->metasize*256;
fprintf(stderr,"; spritesize=%d\n",spritesize);

		if (parameter->nidx1) {
			if (!parameter->nidx2) {
				/* compile full sur un seul fichier */
				for (i=zemax=0;i<parameter->nidx1;i++) {
					if (parameter->idx1[i]>zemax) zemax=parameter->idx1[i];
				}
				/* on a l'index max, alors on peut calculer la taille max à lire */
				data1=malloc((zemax+1)*256);
				FileReadBinary(parameter->filename1,data1,256*(zemax+1));

				for (j=0;j<parameter->nidx1;j++) {
					/* copie du sprite courant depuis l'index idx1[j] */
				
					compilation_action.sp1=malloc(spritesize);
					compilation_action.sp2=malloc(spritesize);
					compilation_action.lng=spritesize;

					memcpy(compilation_action.sp2,data1+parameter->idx1[j]*256,spritesize);
					for (i=0;i<spritesize;i++) {
						compilation_action.sp1[i]=compilation_action.sp2[i]+8; // force differences!
					}
					compilation_action.idx=compilation_action.idx2=parameter->idx1[j];
					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
				}
			} else {
				/* compile diff sur deux fichiers avec deux séquences! */
fprintf(stderr,"; compilation MT sur 2 sequences avec support META\n");
				if (parameter->nidx1!=parameter->nidx2) {
					fprintf(stderr,"pour compiler en diff deux séquences il faut deux séquences avec le même nombre d'indexes (%d != %d)\n",parameter->nidx1,parameter->nidx2);
					exit(-2);
				}
				for (i=zemax=0;i<parameter->nidx1;i++) {
					if (parameter->idx1[i]>zemax) zemax=parameter->idx1[i];
				}
				data1=malloc((zemax+1)*spritesize);
				FileReadBinary(parameter->filename1,data1,spritesize*(zemax+1));
				for (i=zemax=0;i<parameter->nidx2;i++) {
					if (parameter->idx2[i]>zemax) zemax=parameter->idx2[i];
				}
				data2=malloc((zemax+1)*spritesize);
				FileReadBinary(parameter->filename2,data2,spritesize*(zemax+1));

				for (j=0;j<parameter->nidx1;j++) {
					compilation_action.sp1=malloc(spritesize);
					compilation_action.sp2=malloc(spritesize);

					memcpy(compilation_action.sp1,data1+parameter->idx1[j]*spritesize,spritesize);
					memcpy(compilation_action.sp2,data2+parameter->idx2[j]*spritesize,spritesize);
					compilation_action.idx=parameter->idx1[j];
					compilation_action.idx2=parameter->idx2[j];
					compilation_action.lng=spritesize;
					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
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
					/*  Le label c'est bon UNIQUEMENT en cas de compilation unique... */
					if (parameter->label) {
						printf("%s%d:\n",parameter->label,idx);
					}
					idx++;
				} else {
					ok=0;
				}
				if (parameter->compilediff) {
					compilation_action.sp1=malloc(spritesize);
					compilation_action.sp2=malloc(spritesize);

					FileReadBinary(filename1,compilation_action.sp1,spritesize);
					FileReadBinary(filename2,compilation_action.sp2,spritesize);
					compilation_action.idx=idx-1;
					compilation_action.idx2=idx;
					compilation_action.lng=spritesize;

					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
				} else if (parameter->compilefull) {
					compilation_action.idx2=compilation_action.idx=idx-1;
					compilation_action.sp1=malloc(spritesize);
					compilation_action.sp2=malloc(spritesize);
					compilation_action.lng=spritesize;
					
					FileReadBinary(filename1,compilation_action.sp2,spritesize);
					for (i=0;i<spritesize;i++) {
						compilation_action.sp1[i]=compilation_action.sp2[i]+8; // force differences!
					}
					ObjectArrayAddDynamicValueConcat((void**)&compilation_actions,&nbcaction,&maxcaction,&compilation_action,sizeof(compilation_action));
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
				
	// pas de liberation memoire, mode gros porc...
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
	
	printf("%.*s.exe v1.5 / Edouard BERGE 2023-04\n",(int)(sizeof(__FILENAME__)-3),__FILENAME__);
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
	printf("-c                  compile a full sprite\n");
	printf("-d                  compile difference between two sprites or a sequence\n");
	printf("-meta <auto|nb>     compile consecutive sprites\n");
	printf("-noret              do not add RET at the end of the routine\n");
	printf("-exhlde             add a EX HL,DE at the beginning of the routine\n");
	printf("-inch               add a INC H at the end of the routine\n");
	printf("-jpix               add a JP (IX) at the end of the routine\n");
	printf("-jpiy               add a JP (IY) at the end of the routine\n");
	printf("-jp <nn>            add a JP <label or address> at the end of the routine\n");
	printf("-l <label>          insert a numbered label for multi diff\n");
	printf("-alist              create a full ready-to-use list for animation core\n");
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
		if (valend<valstar) {
			for (j=valstar;j>=valend;j--) IntArrayAddDynamicValueConcat(&idx,&nidx,&midx,j);
		} else {
			for (j=valstar;j<=valend;j++) IntArrayAddDynamicValueConcat(&idx,&nidx,&midx,j);
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
	} else if (strcmp(argv[i],"-idx")==0) {
		if (i+1<argc) {
			GetSequence(parameter,argv[++i]);
		} else Usage();
	} else if (strcmp(argv[i],"-alist")==0) {
		parameter->animate=1;
	} else if (strcmp(argv[i],"-meta")==0) {
		parameter->meta=1;
		if (i+1<argc) {
			i++;
			parameter->metasize=atoi(argv[i]); // auto=0
		} else {
			fprintf(stderr,"; META option needs AUTO or a positive non null value\n");
			exit(-1);
		}
	} else if (strcmp(argv[i],"-noret")==0) {
		parameter->noret=1;
	} else if (strcmp(argv[i],"-jpix")==0) {
		parameter->jpix=1;
	} else if (strcmp(argv[i],"-jpiy")==0) {
		parameter->jpiy=1;
	} else if (strcmp(argv[i],"-jp")==0) {
		parameter->jpmode=1;
		if (i+1<argc) {
			parameter->absolute=argv[++i];
		} else Usage();
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



