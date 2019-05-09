
/*
 * metronomo.c
 * Aplicación que procesa un fichero de vídeo y
 * analiza el audio para detectar el tiempo (los pulsos por minuto o beats per minute)
 * y aplica efectos de vídeo en función de dicho tiempo.
 *
 * Proyecto GStreamer - Aplicaciones Multimedia (2018-19)
 * Universidad Carlos III de Madrid
 *
 * Equipo:
 * - Alumno 1: Guillermo García Castaño NIA: 100330232
 *
 * Versión implementada (TO DO: eliminar las líneas que no procedan):
 * - versión básica
 * - intervalo
 *
 *
 */

  // ------------------------------------------------------------
  // Procesar argumentos
  // ------------------------------------------------------------

  // REF: https://www.gnu.org/software/libc/manual/html_node/Parsing-Program-Arguments.html#Parsing-Program-Arguments

  /*
   * Argumentos del programa:
   * -h: presenta la ayuda y termina (con estado 0)
   * -i inicio: instante inicial a partir del cual reproducir y realizar la detección (en nanosegundos)
   * -f fin: instante final en el que detener la reproducción y detección (en nanosegundos)
   * -l límite_inferior: umbral inferior, si el tempo detectado es menor que este umbral se aplicará un efecto de vídeo antiguo.
   * -g límite_superior: umbral superior, si el tempo detectado es mayor que este umblar se aplicará un efecto de detección de bordes
   * fichero_entrada: fichero de vídeo a analizar
   */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <gst/gst.h>
#include <string.h>

//Elementos del pipeline
GMainLoop *loop;
GstElement *pipeline, *source, *demuxer, *queue_vid, *queue_aud, *video_dec, *video_conv, *audio_dec, *audio_conv_in, *audio_conv_out, *video_sink, *audio_sink, *bpm_detector;

//Variables globales
int estado = 0;//Estado que se devuelve segun el error
gint64 current_time = -1;
gint64 duracion_video;//Para comprobar el intervalo de tiempo
gint64 tiempo_inicial = -1;//momento en el que comenzara el video
int flag_h = 0;//flag del argumento h
int flag_f = 0;//flag del argumento f
gint64 tiempo_final = -1;//momento en el que finalizara el video
char *caracter_i = NULL;
char *caracter_f = NULL;

 //Obtiene el tiempo en cada momento y finaliza el video si se ha superado el tiempo final.
 //Para las funcionalidades -i y -f
 static gboolean fin_del_video(gpointer time)
 {
   if(tiempo_final > 0)
   {
     gst_element_query_position(time, GST_FORMAT_TIME, &current_time);
  		if (tiempo_final > 0)
      {
  			if (current_time >= tiempo_final)
        {
  				g_main_loop_quit (loop);
  			}
      }
   }
   return TRUE;
 }

// Para comprobar si un string es un nummero
//Para las funcionalidades -i y -f
int checkNum (char * str)
{
    if (str == NULL || *str == '\0' || isspace(*str))
      return 0;
    char * p;
    strtod (str, &p);
    return *p == '\0';
}


// Comprueba los argumentos
//Para las funcionalidades -i y -f
static int checkArg(char *arg)
{
  int lenarg = strlen(arg);
  char letra = arg[0];
  if (lenarg > 4)
  {
    const char *last_four = &arg[lenarg-4];
    if(strcmp(last_four,".mp4") == 0)//nos asegura que el video es de formato .mp4
    {
   	  return -1;
   	}
   	else
    {
   	  return 1;
   	}
   }
   	else if (letra =='-')
    {
   		return -1;
   	}
   	else
    {
   		return 1;
   	}
}

//Controla el bus
static gboolean bus_call (GstBus     *bus,  GstMessage *msg, gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;
  char *src = GST_MESSAGE_SRC_NAME(msg);
  gdouble bpm;
  GstTagList *tags;
  gint64 current_time;

  switch (GST_MESSAGE_TYPE (msg))
  {
       case GST_MESSAGE_EOS:
       {
         estado = 0;
         g_print ("..[bus].. (%s) :: End of stream\n", src);
         g_main_loop_quit (loop);
         break;
       }
       case GST_MESSAGE_ERROR:
         {
         gchar  *debug;
         GError *error;
         gst_message_parse_error (msg, &error, &debug);
         g_free (debug);
         g_printerr ("..[bus].. (%s) :: Error: %s\n", src, error->message);
         g_error_free (error);
         estado = -1;  //error en el procesamiento multimedia.
         g_main_loop_quit (loop);
         break;
         }
       case GST_MESSAGE_TAG:
         {
         gst_message_parse_tag (msg, &tags); //Con esto obtenemos la lista de tags obtenidos
         if(tags!=NULL)
         {
          if(gst_tag_exists(GST_TAG_BEATS_PER_MINUTE))//comprueba si existe el tag "beats-per-minute"
          {
            if(gst_tag_list_get_double(tags, GST_TAG_BEATS_PER_MINUTE, &bpm))//retorna el bpm
            {
              gst_element_query_position(pipeline, GST_FORMAT_TIME, &current_time);
              g_print("Tempo: %f pulsos-por-minuto (%"GST_TIME_FORMAT")\n", bpm, GST_TIME_ARGS(current_time)); //tiempo en minutos
            }
          }
         }
         return TRUE;
         break;
         }
       default:
       {
         //Funcionalidad -i -f
         if (strcmp(GST_MESSAGE_TYPE_NAME(msg),"new-clock") == 0)
         {
           if (tiempo_inicial > 0 )
           {
             if (!gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, tiempo_inicial))//Tiempo en el que se inicia el video
             {
               fprintf(stderr, "Error al seleccinar el inicio del video.\n");
               return 2;
             }
           }
           if (tiempo_final > 0)
           {
             g_timeout_add(100, fin_del_video, pipeline);//Repite la funcion que comprueba si debe acabarse el video
           }
         }
         g_print ("..[bus].. %15s :: %-15s\n", src, GST_MESSAGE_TYPE_NAME(msg));
         break;
       }
     }
     gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duracion_video);//guarda la duracion del video en duracion_video
     if(duracion_video < tiempo_final && duracion_video!=0)
     {
       fprintf(stderr, "Error: intervalo [%s]--[%s] no valido.\n", caracter_i, caracter_f);
       g_main_loop_quit (loop);
       return 2;
     }
     else if(duracion_video < tiempo_inicial && duracion_video!=0)
     {
       fprintf(stderr, "Error: intervalo [%s]--[%s] no valido.\n", caracter_i, caracter_f);
       g_main_loop_quit (loop);
       return 2;
     }
     return TRUE;
}


//Añade los pads
static void on_pad_added (GstElement *element, GstPad     *pad, gpointer    data)
{
  GstPad *sinkpad;
  char *caps = gst_caps_to_string(gst_pad_get_current_caps(pad));
  if (g_str_has_prefix(caps, "audio"))
  {
    sinkpad = gst_element_get_static_pad (queue_aud, "sink");
  }
  else if  (g_str_has_prefix(caps, "video"))
  {
    sinkpad = gst_element_get_static_pad (queue_vid, "sink");
  }
  if (sinkpad != NULL)
  {
    gst_pad_link (pad, sinkpad);
    gst_object_unref (sinkpad);
  }
}



int main(int argc,  char *argv[])
{
  int c;
  char *filename;
  char *temp;
  int intervalOK = 0;// 0 correcto, 1 incorrecto

while ((c = getopt (argc, argv, "h:i:f:l:g:")) != -1)
{
    switch (c)
    {
      case 'h':
        flag_h = 1;
        break;

      case 'i':
         caracter_i = optarg;
         if(checkArg(optarg) == -1)
         {
           fprintf(stderr, "Error: la opción [i] requiere un argumento\n");
           return 1;
         }
        if(optarg == NULL)
        {
           g_print("Por favor, introduzca un instante inicial\n");
           intervalOK = 1;
        }
        else if(*optarg == '\0')
        {
           g_print("por favor, acabe el código\n");
           intervalOK = 1;
        }
        else if(isspace(*optarg))
        {
           g_print("No introduzca un espacio como instante inicial\n");
           intervalOK = 1;
        }
        else
        {
          strtod(optarg,&temp);
          tiempo_inicial = g_ascii_strtoull(optarg, NULL, 10);
        }
        break;

      case 'f':
        caracter_f = optarg;
        if(checkArg(optarg) == -1)
        {
          fprintf(stderr, "Error: la opción [f] requiere un argumento\n");
          return 1;
        }
       if(optarg == NULL)
       {
          g_print("Por favor, introduzca un instante final\n");
          intervalOK = 1;
       }
       else if(*optarg == '\0')
       {
          g_print("por favor, acabe el código\n");
          intervalOK = 1;
       }
       else if(isspace(*optarg))
       {
          g_print("No introduzca un espacio como instante final\n");
          intervalOK = 1;
       }
       else
       {
         strtod(optarg,&temp);
         tiempo_final = g_ascii_strtoull(optarg, NULL, 10);
       }
       break;

      case 'l':
        // umbral inferior para efectos de vídeo
        g_print("Funcionalidad efectos no implementada\n");
        break;

      case 'g':
        // umbral superior para efectos de vídeo
        g_print("Funcionalidad efectos no implementada\n");
        break;

      case '?':// getopt devuelve '?' si encuentra una opción desconocida
        if (isprint (optopt))
        {
          fprintf (stderr, "Error: argumento [-%c] no válido\n", optopt);
        }
        else
        {
          fprintf (stderr, "Error: argumento [\\x%x] no válido.\n", optopt);
        }
        return 1;

      default:
          fprintf (stderr, "Error: argumento %d no válido\n", optind);
          return 1;
    }
  }
  if (intervalOK == 1)//Compribacion de errores
  {
    fprintf (stderr,"Error: intervalo %s--%s] no válido.\n",caracter_i,caracter_f);
    g_main_loop_quit (loop);
    return 2;
  }


if(flag_h == 1)//En de ejecutar con -h se muestra esto
{
  g_printerr("\n Use la siguiente estructura: ./metronomo [-h] [-i inicio] [-f fin] [-l limite_inferior] [-g limite_superior] fichero_entrada\n");
  g_printerr(" Descripcion de cada argumento: \n");
  g_printerr("  -h: presenta la ayuda y termina (con estado 0)\n");
  g_printerr("  -i inicio: instante inicial a partir del cual reproducir y realizar la detección (en nanosegundos).\n");
  g_printerr("  -f fin: instante final en el que detener la reproduccion y deteccion (en nanosegundos).\n");
  g_printerr("  -l limite_inferior: umbral inferior, si el tempo detectado es menor que este umbral se aplicará un efecto de vídeo antiguo.\n");
  g_printerr("  -g limite_superior: umbral superior, si el tempo detectado es mayor que este umblar se aplicará un efecto de detección de bordes.\n");
  g_printerr("  fichero_entrada: fichero de video a analizar\n\n\n");
  estado=0;
  return estado;
}

filename = argv[optind];
if (checkArg(filename) != -1)//Comprobacion de errores
{
  fprintf(stderr, "Error: argumento %s no válido (se requiere un formato mp4)\n", filename);
  return 1;
}

int index;
  for (index = optind+1; index < argc; index++)//Recorre todos los argumentos
  {
    fprintf (stderr, "Error: argumento %s no valido\n", argv[index]);
    return 1;
  }
puts("GStreamer test - init!\n");
gst_init(&argc, &argv);//Inicia la libreria de gstreamer
loop = g_main_loop_new (NULL, FALSE);

gst_init (&argc, &argv);//Crea el pipeline
loop = g_main_loop_new(NULL,FALSE);

//Creacion de elementos
pipeline = gst_pipeline_new("pipeline");
source = gst_element_factory_make("filesrc","source");
demuxer = gst_element_factory_make("qtdemux","demux");
queue_vid = gst_element_factory_make("queue","queue_vid");
queue_aud = gst_element_factory_make("queue","queue_aud");
video_dec = gst_element_factory_make("avdec_h264","video_dec");
video_conv = gst_element_factory_make("videoconvert","vid_conv");
audio_dec = gst_element_factory_make("faad","audio_dec");
audio_conv_in = gst_element_factory_make("audioconvert","audio_conv_in");
audio_conv_out = gst_element_factory_make("audioconvert","audio_conv_out");
video_sink = gst_element_factory_make("ximagesink","vid_sink");
audio_sink = gst_element_factory_make("alsasink","audio_sink");
bpm_detector = gst_element_factory_make("bpmdetect","bpm_detector");

//Comprueba errores en la creacion de los elementos
if (!pipeline || !source || !demuxer || !queue_vid || !video_dec || !video_conv || !video_sink || !queue_aud || !audio_dec || !audio_conv_in || !bpm_detector || !audio_conv_out || !audio_sink)
{
  g_printerr ("One element could not be created. Exiting.\n");
  return -1;
}

g_object_set(G_OBJECT(source), "location", filename, NULL);//Filename en el source
g_object_set (G_OBJECT (demuxer), "name", "demux", NULL);//Nombre para el demuxer
g_object_set (G_OBJECT (bpm_detector), "name", "bpm_detector", NULL);


//Sistema de mensajes del bus
GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
gst_bus_add_watch (bus, bus_call, loop);
gst_object_unref (bus);



//Añadimos elementos al pipeline
gst_bin_add_many(GST_BIN(pipeline), source, demuxer, queue_vid, video_dec, video_conv, video_sink, queue_aud, audio_dec, audio_conv_in, bpm_detector, audio_conv_out, audio_sink, NULL);

//Unimos elementos
gst_element_link(source, demuxer);
gst_element_link_many(queue_vid, video_dec, video_conv, video_sink, NULL);
gst_element_link_many(queue_aud, audio_dec, audio_conv_in, bpm_detector, audio_conv_out, audio_sink, NULL);
g_signal_connect (demuxer, "pad-added", G_CALLBACK (on_pad_added), NULL);//Conecta el demuxer con el resto del pipeline


g_print("Play: %s\n", argv[1]);
gst_element_set_state (pipeline, GST_STATE_PLAYING);

g_print ("Run...\n");
g_main_loop_run (loop);

g_print ("Returned, stopping playback\n");

gst_element_set_state (pipeline, GST_STATE_NULL);

g_print ("Deleting pipeline\n");
gst_object_unref (GST_OBJECT (pipeline));//Libera memoria
puts("\nGStreamer test - end!");

    return estado;
}
