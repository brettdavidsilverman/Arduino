#ifndef BEE_FISH__MISCELLANEOUS__LOG_HPP
#define BEE_FISH__MISCELLANEOUS__LOG_HPP

#ifdef LOG
#include <syslog.h>
#endif

#include <unistd.h>
#include <stdarg.h>

// Should log to /var/log/messages

namespace BeeFishMisc {

   void openLog() {
#ifdef LOG
      setlogmask (LOG_UPTO (LOG_NOTICE));

      openlog (NULL, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

      syslog (LOG_NOTICE, "Program started by User %d", getuid ());
#endif
   }

   template<typename ... Args>
   void logMessage(int priority, const std::string& message, Args ... args)
   {
#ifdef LOG
      //fprintf(stderr, message.c_str(), args ...);
      //fprintf(stderr, "\n");

      syslog(priority, message.c_str(), args ...);
#endif      
   }

   void closeLog() {
#ifdef LOG      
      closelog();
#endif      
   }

}


#endif