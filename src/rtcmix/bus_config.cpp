/* RTcmix  - Copyright (C) 2000  The RTcmix Development Team
   See ``AUTHORS'' for a list of contributors. See ``LICENSE'' for
   the license to this software and for a DISCLAIMER OF ALL WARRANTIES.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ugens.h>
#include <bus.h>
#include <globals.h>
#include <prototypes.h>
#include <lock.h>
  
/* #define PRINTPLAY */
/* #define DEBUG  */
/* #define PRINTALL */

//
// BusSlot "class" methods
//

BusSlot::BusSlot() : next(NULL), prev(NULL), in_count(0), out_count(0),
					 auxin_count(0), auxout_count(0), refcount(0)
{
	for (int n=0; n<MAXBUS; ++n)
	    in[n] = out[n] = auxin[n] = auxout[n] = 0;
}

//
// BusQueue class methods
//

BusQueue::BusQueue(char *name, BusSlot *theQueue)
		: inst_name(strdup(name)), queue(theQueue), next(NULL) {}

BusQueue::~BusQueue() { free(inst_name); }

/* Special flags and whatnot */
typedef enum {
  CONFIG,
  NOT_CONFIG
} ConfigStatus;

/* List of lists of BusSlots used by Insts to get their bus_config setup */
static BusQueue *Inst_Bus_Config;

/* Flag to tell us if we've gotten any configs */
/* Used to initialize Bus_In_Config inside check_bus_inst_config */
static Bool Bus_Config_Status = NO;

/* Bus graph, parsed by check_bus_inst_config */
/* Allows loop checking ... and buffer playback order? */
static CheckNode *Bus_In_Config[MAXBUS];

/* Global private arrays */
static Bool HasChild[MAXBUS];
static Bool HasParent[MAXBUS];
static Bool AuxInUse[MAXBUS];
static Bool AuxOutInUse[MAXBUS];
static Bool OutInUse[MAXBUS];
static short RevPlay[MAXBUS];

/* Prototypes (in order)------------------------------------------------------*/
static int strtoint(char*, int *);  /* Helper */
static void print_bus_slot(BusSlot *);  /* Debugging */
static ErrCode print_inst_bus_config();
static void print_parents();
static void print_children();
static void print_play_order();
static ErrCode check_bus_inst_config(BusSlot*, Bool);  /* Graph parsing, insertion */
static ErrCode insert_bus_slot(char*, BusSlot*);
static void bf_traverse(int, Bool);
static void create_play_order();
static ErrCode parse_bus_chan(char*, int*, int*); /* Input processing */
extern "C" double bus_config(float*, int, double*);

/* ------------------------------------------------------------- strtoint --- */
static INLINE int
strtoint(char *str, int *num)
{
   long  along;
   char  *pos;

   pos = NULL;
   errno = 0;
   along = strtol(str, &pos, 10);
   if (pos == str)                           /* no digits to convert */
      return -1;
   if (errno == ERANGE)                      /* overflow or underflow */
      return -1;

   *num = (int)along;
   return 0;
}

/* ------------------------------------------------------- print_parents -----*/
static void
print_parents() {
  int i;
  printf("Aux buses w/o aux inputs:  "); 
  for(i=0;i<MAXBUS;i++) {
	pthread_mutex_lock(&aux_in_use_lock);
	if (AuxInUse[i]) {
	  pthread_mutex_lock(&has_parent_lock);
	  if (!HasParent[i]) {
		printf(" %d",i);
	  }
	  pthread_mutex_unlock(&has_parent_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
  printf("\n");
}

/* ------------------------------------------------------ print_children -----*/
static void
print_children() {
  int i;
  printf("Aux buses w/o aux outputs:  "); 
  for(i=0;i<MAXBUS;i++) {
	pthread_mutex_lock(&aux_in_use_lock);
	if (AuxInUse[i]) {
	  pthread_mutex_lock(&has_child_lock);
	  if (!HasChild[i]) {
		printf(" %d",i);
	  }
	  pthread_mutex_unlock(&has_child_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
  printf("\n");
}

/* ------------------------------------------------------- print_bus_slot --- */
static void
print_bus_slot(BusSlot *bs)
{
   int i;

   printf("\n   in_count=%d :", bs->in_count);
   for (i = 0; i < bs->in_count; i++)
      printf(" %d", bs->in[i]);
   printf("\n   out_count=%d :", bs->out_count);
   for (i = 0; i < bs->out_count; i++)
      printf(" %d", bs->out[i]);
   printf("\n   auxin_count=%d :", bs->auxin_count);
   for (i = 0; i < bs->auxin_count; i++)
      printf(" %d", bs->auxin[i]);
   printf("\n   auxout_count=%d :", bs->auxout_count);
   for (i = 0; i < bs->auxout_count; i++)
      printf(" %d", bs->auxout[i]);
   printf("\n   refcount=%d\n", bs->refcount);
}

/* ----------------------------------------------------- print_bus_config --- */
/* Prints config from Inst. point of view */
static ErrCode
print_inst_bus_config() {
   BusQueue *t_array;
   BusSlot *check_q;

   pthread_mutex_lock(&inst_bus_config_lock);
   t_array = Inst_Bus_Config;
   pthread_mutex_unlock(&inst_bus_config_lock);

   while (t_array) {

	  printf("%s",t_array->instName());
	  check_q = t_array->queue;
	  
	  if (check_q == NULL) {
		 printf("done\n");
		 return NO_ERR;
	  }
	  
	  while(check_q) {
		 print_bus_slot(check_q);
		 check_q = check_q->next;
	  }
	  
	  t_array = t_array->next;
	  
   }
   return NO_ERR;
}

/* ----------------------------------------------------- print_play_order --- */
static void
print_play_order() {
  int i;
  printf("Output buffer playback order:  ");
  for(i=0;i<MAXBUS;i++) {
	pthread_mutex_lock(&aux_to_aux_lock);
	if (AuxToAuxPlayList[i] != -1) {
	  printf(" %d",AuxToAuxPlayList[i]);
	}
	pthread_mutex_unlock(&aux_to_aux_lock);
  }
  printf("\n");
}

/* ------------------------------------------------ check_bust_inst_config -- */
/* Parses bus graph nodes */

static ErrCode
check_bus_inst_config(BusSlot *slot, Bool visit) {
  int i,j,aux_ctr,out_ctr;
  short *in_check_list;
  short in_check_count;
  CheckQueue *in_check_queue,*t_queue,*last;
  CheckNode *t_check_node,*t_node;
  short t_in,t_out;
  static Bool Visited[MAXBUS];
  Bool Checked[MAXBUS];
  short r_p_count=0;

  /* If we haven't gotten a config yet ... allocate the graph array */
  /* and the playback order list */
  pthread_mutex_lock(&bus_config_status_lock);
  if (Bus_Config_Status == NO) {
	for (i=0;i<MAXBUS;i++) {
	  t_node = new CheckNode;
	  pthread_mutex_lock(&bus_in_config_lock);
	  Bus_In_Config[i] = t_node;
	  pthread_mutex_unlock(&bus_in_config_lock);
	}
	Bus_Config_Status = YES;
  }
  pthread_mutex_unlock(&bus_config_status_lock);


  aux_ctr = out_ctr = 0;
  j=0;
  for(i=0;i<MAXBUS;i++) {
	if (visit)
	  Visited[i] = NO;
	Checked[i] = NO;
	pthread_mutex_lock(&revplay_lock);
	RevPlay[i] = -1;
	pthread_mutex_unlock(&revplay_lock);
	pthread_mutex_lock(&out_in_use_lock);
	if (OutInUse[i]) {  // DJT For scheduling
	  pthread_mutex_lock(&to_out_lock);
	  ToOutPlayList[out_ctr++] = i;
	  pthread_mutex_unlock(&to_out_lock);
	}
	pthread_mutex_unlock(&out_in_use_lock);
	pthread_mutex_lock(&aux_out_in_use_lock);
	if (AuxOutInUse[i]) {
	  pthread_mutex_lock(&to_aux_lock);
	  ToAuxPlayList[aux_ctr++] = i;
	  pthread_mutex_unlock(&to_aux_lock);
	}
	pthread_mutex_unlock(&aux_out_in_use_lock);
  }

  /* Put the slot being checked on the list of "to be checked" */
  t_node = new CheckNode(slot->auxin, slot->auxin_count);
  in_check_queue = new CheckQueue(t_node);
  last = in_check_queue;

  /* Go down the list of things (nodes) to be checked */
  while (in_check_queue) {
	t_check_node = in_check_queue->node;
	in_check_list = t_check_node->bus_list;
	in_check_count = t_check_node->bus_count;
	
	for (i=0;i<in_check_count;i++) {
	  t_in = in_check_list[i];
	  
	  /* Compare to each of the input slot's output channels */
	  for (j=0;(j<slot->auxout_count) && (!Checked[t_in]);j++) {
		t_out = slot->auxout[j];
#ifdef PRINTALL
		printf("checking in=%d out=%d\n",t_in,t_out);
#endif

		/* If they're equal, then return the error */
		if (t_in == t_out) {
		  rterror(NULL, "ERROR:  bus_config loop ... config not allowed.\n");
		  return LOOP_ERR;
		}
	  }
	  if (!Checked[t_in]) {
		Checked[t_in] = YES;
	  }

	  /* If this input channel has other input channels */
	  /* put them on the list "to be checked" */
	  
	  pthread_mutex_lock(&bus_in_config_lock);
	  if ((Bus_In_Config[t_in]->bus_count > 0) && (!Visited[t_in])) {
#ifdef PRINTALL
		printf("adding Bus[%d] to list\n",t_in);
#endif
		pthread_mutex_lock(&has_parent_lock);
		if (HasParent[t_in]) {
#ifdef PRINTPLAY
		  printf("RevPlay[%d] = %d\n",r_p_count,t_in);
#endif
		  pthread_mutex_lock(&revplay_lock);
		  RevPlay[r_p_count++] = t_in;
		  pthread_mutex_unlock(&revplay_lock);
		}
		pthread_mutex_unlock(&has_parent_lock);
		Visited[t_in] = YES;
		t_queue = new CheckQueue(Bus_In_Config[t_in]);
		last->next = t_queue;
		last = t_queue;
	  }
	  pthread_mutex_unlock(&bus_in_config_lock);
	}
#ifdef PRINTALL
	printf("popping ...\n");
#endif
	in_check_queue = in_check_queue->next;
  }

  return NO_ERR;
}

/* ------------------------------------------------------ insert_bus_slot --- */
/* Inserts bus configuration into structure used by insts */
/* Also inserts into bus graph */
/* Special case when called by bf_traverse->check_bus_inst_config-> */
/*     s_in set to 333 and filtered out below */
static ErrCode
insert_bus_slot(char *name, BusSlot *slot) {
  
  BusQueue *t_array,*t_queue;
  BusSlot *check_q;
  short i,j,t_in_count,s_in,s_out;

  /* Insert into bus graph */
  for(i=0;i<slot->auxout_count;i++) {
	s_out = slot->auxout[i];
	pthread_mutex_lock(&aux_in_use_lock);
	if (!AuxInUse[s_out]) {
	  AuxInUse[s_out] = YES;
	}
	pthread_mutex_unlock(&aux_in_use_lock);
	for(j=0;j<slot->auxin_count;j++) {
	  s_in = slot->auxin[j];
	  pthread_mutex_lock(&has_parent_lock);
	  if ((!HasParent[s_out]) && (s_in != 333)) {
#ifdef PRINTALL
		printf("HasParent[%d]\n",s_out);
#endif
		HasParent[s_out] = YES;
	  }
	  pthread_mutex_unlock(&has_parent_lock);

	  pthread_mutex_lock(&bus_in_config_lock);
	  t_in_count = Bus_In_Config[s_out]->bus_count;
	  pthread_mutex_unlock(&bus_in_config_lock);
#ifdef PRINTALL
	  printf("Inserting Bus_In[%d] = %d\n",s_out,s_in);
#endif
	  if (s_in != 333) {
		pthread_mutex_lock(&bus_in_config_lock);
		Bus_In_Config[s_out]->bus_list[t_in_count] = s_in;
		Bus_In_Config[s_out]->bus_count++;
		pthread_mutex_unlock(&bus_in_config_lock);
		pthread_mutex_lock(&has_child_lock);
		HasChild[s_in] = YES;
		pthread_mutex_unlock(&has_child_lock);
		pthread_mutex_lock(&aux_in_use_lock);
		AuxInUse[s_in] = YES;
		pthread_mutex_unlock(&aux_in_use_lock);
	  }
	}
  }

  /* Create initial node for Inst_Bus_Config */
  pthread_mutex_lock(&inst_bus_config_lock);
  if (Inst_Bus_Config == NULL) {
	Inst_Bus_Config = new BusQueue(name, slot);
	pthread_mutex_unlock(&inst_bus_config_lock);
	return NO_ERR;
  }
  t_array = Inst_Bus_Config;
  pthread_mutex_unlock(&inst_bus_config_lock);
  
  Lock lock(&inst_bus_config_lock);	// unlocks when out of scope

  /* Traverse down each list */
  while(t_array) {	
	/* If names match, then put onto the head of the slot's list */
	if (strcmp(t_array->instName(), name) == 0) {
	  slot->next = t_array->queue;
	  t_array->queue = slot;
	  return NO_ERR;
	}
	
	/* We've reached the end ... so put a new node on with inst's name */
	if (t_array->next == NULL) {
	  t_array->next = new BusQueue(name, slot);
  	  return NO_ERR;
	}
	t_array = t_array->next;
  }
  return NO_ERR;
}


/* ----------------------------------------------------- bf_traverse -------- */
/* sets fictitious parent node to 333 */
/* filtered out in insert() */
static void
bf_traverse(int bus, Bool visit) {
  BusSlot *temp = new BusSlot;
  temp->auxin[0] = bus;
  temp->auxin_count=1;
  temp->auxout[0] = 333;
  temp->auxout_count=1;
  check_bus_inst_config(temp, visit);
  delete temp;
}

/* ----------------------------------------------------- create_play_order -- */
static void create_play_order() {
  int i,j;
  Bool visit = YES;
  short aux_p_count = 0;

  /* Put all the parents on */
  for(i=0;i<MAXBUS;i++) {
	pthread_mutex_lock(&aux_in_use_lock);
	if (AuxInUse[i]) {
	  pthread_mutex_lock(&has_parent_lock);
	  if (!HasParent[i]) {
#ifdef PRINTPLAY
		printf("AuxPlay[%d] = %d\n",aux_p_count,i);
#endif
		pthread_mutex_lock(&aux_to_aux_lock);
		AuxToAuxPlayList[aux_p_count++] = i;
		pthread_mutex_unlock(&aux_to_aux_lock);
	  }
	  pthread_mutex_unlock(&has_parent_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
  for (i=0;i<MAXBUS;i++) {
	pthread_mutex_lock(&aux_in_use_lock);
	if (AuxInUse[i]) {
	  pthread_mutex_lock(&has_child_lock);
	  if (!HasChild[i]) {
#ifdef PRINTPLAY
		printf("bf_traverse(%d)\n",i);
#endif
		bf_traverse(i,visit);
		if (visit) 
		  visit = NO;
		for (j=MAXBUS-1;j>=0;j--) {
		  pthread_mutex_lock(&revplay_lock);
		  if (RevPlay[j] != -1) {
#ifdef PRINTPLAY
			printf("AuxPlay[%d](%d) = Rev[%d](%d)\n",
					aux_p_count,AuxToAuxPlayList[aux_p_count],j,RevPlay[j]);
#endif
			pthread_mutex_lock(&aux_to_aux_lock);
			AuxToAuxPlayList[aux_p_count++] = RevPlay[j];
			pthread_mutex_unlock(&aux_to_aux_lock);
		  }
		  pthread_mutex_unlock(&revplay_lock);
		}
	  }
	  pthread_mutex_unlock(&has_child_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
}

/* ------------------------------------------------------- get_bus_config --- */
/* Given an instrument name, return a pointer to the most recently
   created BusSlot node for that instrument name. If no instrument name
   match, return a pointer to the default node.
*/
BusSlot *
get_bus_config(const char *inst_name)
{
   BusSlot  *slot, *default_bus_slot;
   BusQueue *q;
   ErrCode     err;
   int index,in_chans,i;

   assert(inst_name != NULL);

   slot = NULL;

   /* Maybe also need to lock q since it's accessing a BusSlot */
   /* that intraverse might also be checking? */
   /* But the values don't change, so I don't see why */

   pthread_mutex_lock(&inst_bus_config_lock);
   for (q = Inst_Bus_Config; q; q = q->next) {
	 if (strcmp(inst_name, q->instName()) == 0) {
	   pthread_mutex_unlock(&inst_bus_config_lock);   
	   return q->queue;
	 }
   }
   pthread_mutex_unlock(&inst_bus_config_lock);
   
   /* Default bus_config for backwards compatibility with < 3.0 scores */
   
   warn(NULL, "No bus_config defined, setting default (in/out).");
   
   /* Some init stuff normally done in check_bus_inst_config */
   pthread_mutex_lock(&bus_config_status_lock);
   if (Bus_Config_Status == NO) {
	 for (i=0;i<MAXBUS;i++) {
	   pthread_mutex_lock(&aux_to_aux_lock);
	   AuxToAuxPlayList[i] = -1;
	   pthread_mutex_unlock(&aux_to_aux_lock);
	   pthread_mutex_lock(&to_aux_lock);
	   ToAuxPlayList[i] = -1;
	   pthread_mutex_unlock(&to_aux_lock);
	   pthread_mutex_lock(&to_out_lock);
	   ToOutPlayList[i] = -1;
	   pthread_mutex_unlock(&to_out_lock);
	   pthread_mutex_lock(&out_in_use_lock);
	   OutInUse[i] = NO;
	   pthread_mutex_unlock(&out_in_use_lock);
	 }
	 Bus_Config_Status = YES;
   }
   pthread_mutex_unlock(&bus_config_status_lock);

   for(i=0;i<NCHANS;i++) {
	 pthread_mutex_lock(&out_in_use_lock);
	 OutInUse[i] = YES;
	 pthread_mutex_unlock(&out_in_use_lock);
	 pthread_mutex_lock(&to_out_lock);
	 ToOutPlayList[i] = i;
	 pthread_mutex_unlock(&to_out_lock);
   }

   default_bus_slot = new BusSlot;
   /* Grab input chans from file descriptor table */
   index = get_last_input_index();
   /* Otherwise grab from audio device, if active */
   if (index == -1) {
	 if (full_duplex)
	   in_chans = NCHANS;
	 else
	   in_chans = 0;
   }
   else
     in_chans = inputFileTable[index].chans;
   
   default_bus_slot->in_count = in_chans;
   default_bus_slot->out_count = NCHANS;
   
   for(i=0;i<in_chans;i++) {
	 default_bus_slot->in[i] = i;
   }
   for(i=0;i<NCHANS;i++) {
	 default_bus_slot->out[i] = i;
   }

   err = check_bus_inst_config(default_bus_slot, YES);
   if (!err) {
      err = insert_bus_slot((char *)inst_name, default_bus_slot);
   }
   if (err)
      exit(1);        /* This is probably what user wants? */

   return default_bus_slot;
}

/* ------------------------------------------------------- parse_bus_chan --- */
static ErrCode
parse_bus_chan(char *numstr, int *startchan, int *endchan)
{
   char  *p;

   if (strtoint(numstr, startchan) == -1)
      return INVAL_BUS_CHAN_ERR;

   p = strchr(numstr, '-');
   if (p) {
      p++;                                           /* skip over '-' */
      if (strtoint(p, endchan) == -1)
         return INVAL_BUS_CHAN_ERR;
   }
   else
      *endchan = *startchan;

   return NO_ERR;
}

/* ------------------------------------------------------- parse_bus_name --- */
ErrCode
parse_bus_name(char *busname, BusType *type, int *startchan, int *endchan)
{
   char     *p;
   ErrCode  status = NO_ERR;

   switch (busname[0]) {
      case 'i':                                      /* "in*" */
         *type = BUS_IN;
         p = &busname[2];                            /* skip over "in" */
         status = parse_bus_chan(p, startchan, endchan);
         break;
      case 'o':                                      /* "out*" */
         *type = BUS_OUT;
         p = &busname[3];                            /* skip over "out" */
         status = parse_bus_chan(p, startchan, endchan);
         break;
      case 'a':                                      /* "aux*" */
         if (strchr(busname, 'i'))
            *type = BUS_AUX_IN;
         else if (strchr(busname, 'o'))
            *type = BUS_AUX_OUT;
         else
            return INVAL_BUS_ERR;
         p = &busname[3];                            /* skip over "aux" */
         status = parse_bus_chan(p, startchan, endchan);
         break;
      default:
	  	 warn("bus_config", "Invalid bus specifier: '%s'", busname);
         return INVAL_BUS_ERR;
   }
   if (status != NO_ERR)
		warn("bus_config", "Invalid bus specifier: '%s'", busname);
   return status;
}

// D.S. This is now a C++ function declared extern "C"

/* ----------------------------------------------------------- bus_config --- */
double bus_config(float p[], int n_args, double pp[])
{
   ErrCode     err;
   int         i, j, k, anint, startchan, endchan;
   char        *str, *instname, *busname;
   BusType     type;
   BusSlot     *bus_slot;
   char		   inbusses[80], outbusses[80];	// for verbose message

   if (n_args < 2)
      die("bus_config", "Wrong number of args.");

   bus_slot = new BusSlot;
   if (bus_slot == NULL)
      return -1.0;

   inbusses[0] = outbusses[0] = '\0';
   
   Lock localLock(&bus_slot_lock);	// This will unlock when going out of scope.

   /* do the old Minc casting rigamarole to get string pointers from a double */
   anint = (int)pp[0];
   str = (char *)anint;
   instname = strdup(str);

   for (i = 1; i < n_args; i++) {
      anint = (int)pp[i];
      busname = (char *)anint;
      err = parse_bus_name(busname, &type, &startchan, &endchan);
      if (err)
         goto error;

      switch (type) {
         case BUS_IN:
			if (bus_slot->in_count > 0) strcat(inbusses, ", ");
			strcat(inbusses, busname);
            if (bus_slot->auxin_count > 0) {
                die("bus_config",
                      "Can't have 'in' and 'aux-in' buses in same bus_config.");
            }
            j = bus_slot->in_count;
            for (k = startchan; k <= endchan; k++)
               bus_slot->in[j++] = k;
            bus_slot->in_count += (endchan - startchan) + 1;
            break;
         case BUS_OUT:
			if (bus_slot->out_count > 0) strcat(outbusses, ", ");
			strcat(outbusses, busname);
            if (bus_slot->auxout_count > 0) {
                die("bus_config",
                    "Can't have 'out' and 'aux-out' buses in same bus_config.");
            }
            j = bus_slot->out_count;
            for (k = startchan; k <= endchan; k++) {
               bus_slot->out[j++] = k;
			   pthread_mutex_lock(&out_in_use_lock);
               OutInUse[k] = YES;  // DJT added
			   pthread_mutex_unlock(&out_in_use_lock);
            }
            bus_slot->out_count += (endchan - startchan) + 1;

            /* Make sure max output chans set in rtsetparams can accommodate
               the highest output chan number in this bus config.
            */
            if (endchan >= NCHANS) {
               die("bus_config", "You specified %d output channels in rtsetparams, "
                         "but this bus_config \nrequires %d channels.",
                         NCHANS, endchan + 1);
            }
            break;
         case BUS_AUX_IN:
			if (bus_slot->auxin_count > 0) strcat(inbusses, ", ");
			strcat(inbusses, busname);
            if (bus_slot->in_count > 0) {
                die("bus_config",
                      "Can't have 'in' and 'aux-in' buses in same bus_config.");
            }
            j = bus_slot->auxin_count;
            for (k = startchan; k <= endchan; k++)
               bus_slot->auxin[j++] = k;
            bus_slot->auxin_count += (endchan - startchan) + 1;
            break;
         case BUS_AUX_OUT:
			if (bus_slot->auxout_count > 0) strcat(outbusses, ", ");
			strcat(outbusses, busname);
            if (bus_slot->out_count > 0) {
                die("bus_config",
                    "Can't have 'out' and 'aux-out' buses in same bus_config.");
            }
            j = bus_slot->auxout_count;
            for (k = startchan; k <= endchan; k++) {
               bus_slot->auxout[j++] = k;
			   pthread_mutex_lock(&aux_out_in_use_lock);
               AuxOutInUse[k] = YES;
			   pthread_mutex_unlock(&aux_out_in_use_lock);
            }
            bus_slot->auxout_count += (endchan - startchan) + 1;
            break;
		 default:
		 	break;
      }
   }

   err = check_bus_inst_config(bus_slot, YES);
   if (!err) {
      err = insert_bus_slot(instname, bus_slot);
   }
   if (err)
      exit(1);        /* This is probably what user wants? */

   /* Make sure specified aux buses have buffers allocated. */
   for (i = 0; i < bus_slot->auxin_count; i++)
      allocate_aux_buffer(bus_slot->auxin[i], RTBUFSAMPS);
   for (i = 0; i < bus_slot->auxout_count; i++)
      allocate_aux_buffer(bus_slot->auxout[i], RTBUFSAMPS);


#ifdef PRINTALL
   print_children();
   print_parents();
#endif
   create_play_order();
#ifdef PRINTPLAY
   print_play_order();
#endif
#ifdef DEBUG
   err = print_inst_bus_config();
#endif

   advise("bus_config", "(%s) => %s => (%s)", inbusses, instname, outbusses);

   return 0.0;
 error:
   die("bus_config", "Cannot parse arguments.");
   return -1.0;
}
