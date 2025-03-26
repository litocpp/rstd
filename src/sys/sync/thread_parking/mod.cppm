export module parking;

#if defined(__linux__) || defined(_WIN32)
import parking.futex;
#else
import parking.pthread;
#endif

namespace parking
{
#if defined(__linux__) || defined(_WIN32)
export using Parker = futex::Parker;
#else
export using Parker = pthread::Parker;
#endif

} // namespace parking