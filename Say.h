#ifndef SAY_H
#define SAY_H

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

// Output that goes out in one write when the statement ends, so that lines from the relay, the
// event loop, and the play and watcher threads never land inside one another:
//
//   Say () << "Relay listening on port " << port << "\n";
//   Say (std::cerr) << "Error: " << reason << "\n";
class Say
{
    public :

        explicit Say (std::ostream &stream = std::cout)
            : m_stream (stream)
        {
        }

        ~Say ()
        {
            m_stream << m_text.str ();
        }

        Say (const Say &) = delete;
        Say &operator= (const Say &) = delete;

        template <typename T>
        Say &operator<< (const T &value)
        {
            m_text << value;

            return (*this);
        }

    private :

        // Now the data members

        std::ostream       &m_stream;
        std::ostringstream  m_text;
};

// Seconds to one decimal place, always shown: "22.0", not "22".
inline std::string tenths (double seconds)
{
    std::ostringstream text;

    text << std::fixed << std::setprecision (1) << seconds;

    return (text.str ());
}

#endif
