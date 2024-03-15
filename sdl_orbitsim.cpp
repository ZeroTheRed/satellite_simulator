#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <unistd.h>
#include <iostream>
#include <cmath>
#include <fstream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <tuple>
#include <vector>
#include <fcntl.h>
#include <sys/poll.h>

constexpr int DELAY_MS = 10; /*SDL delay time in milliseconds*/

/**
 * @brief Draws a filled circle
 * 
 * Generates a circle of a specified radius, centre point, and fills it with the specified colour
 * 
 * @param renderer: A reference to the SDL renderer
 * @param centre_x: The x-coordinate of the centre of the circle (px)
 * @param centre_y: The y-coordinate of the centre of the circle (px)
 * @param radius: The radius of the circle (px)[int]
 * @param colour: The RGBA-format colour to fill the circle with [SDL_colour]
 * 
 * @return Nothing
 */
void drawFilledCircle(SDL_Renderer* renderer, int centre_x, int centre_y, int radius, SDL_Color colour){
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
    const int diameter = radius * 2;
    for(int w=0; w < diameter; ++w){
        for(int h=0; h <diameter; ++h){
            int dx = radius - w;
            int dy = radius - h;
            if (pow(dx,2) + pow(dy,2) <= pow(radius,2)){
                SDL_RenderDrawPointF(renderer, centre_x + dx, centre_y + dy);
            }
        }
    }
}

/**
 * @brief Calculates the satellite's X and Y coordinates
 * 
 * Receives the angle of the satellite and calculates its X- and Y-coordinates
 * 
 * @param angle: Satellite angle [int]
 * @return [std::tuple] The X- and Y-coordinates of the satellite respectively
*/
std::tuple<float, float> calculate_sat_coordinates(int angle, int altitude=100){
    float sat_x = 300 + (altitude * cos(M_PI * angle / 180.0));
    float sat_y = 300 + (altitude * sin(M_PI * angle / 180.0));
    return {sat_x, sat_y};
}

/**
 * @brief Create a Socket object 
 * 
 * Creates a client socket object and establishes a connection request to the server
 * 
 * @param socket_path: The path to the socket
 * @return [int] The client socket descriptor
 */
int createSocket(const char* socket_path){
    // Create a Unix domain socket
    int client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_socket == -1){
        std::cerr << "Error creating socket" << std::endl;
        return 1;
    }

    // Set up socket address
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, socket_path, sizeof(address.sun_path)-1);

    // connect to the server
    bool connected = False;

    int flags = fcntl(client_socket, F_GETFL, 0);
    if (fcntl(client_socket, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl");
        return 1;
    }

    // counter for connection patience
    int fallback = 0;
    while (!connected)
    {
        printf("Attempting to connect to server... ");
        fflush(stdout);
        if (connect(client_socket, (struct sockaddr*)&address, sizeof(address)) == -1){
            if (errno == ENOENT || errno == ECONNREFUSED) {
                if (fallback > 2){
                    return 0;
                }
                std::cerr << "Retrying connection";
                SDL_Delay(1000);
                ++fallback;
            }
            else {
                std::cerr << "Error connecting to socket: " << strerror(errno) << std::endl;
                close(client_socket);
                return 1;
            }

        }
        else {
            printf("Connection to server established");
            fflush(stdout);
            connected = True;
        }
    }     

    return client_socket;
}

/**
 * @brief Get SDL window ID 
 * 
 * Prints the ID number of the SDL window to stdout
 * 
 * @param win: Pointer to the SDL window generated
 */
void captureSDLWindowID(SDL_Window* win){
    // Produce the ID of the SDL window to send to Python
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(win, &wm_info);
    printf("ID: %u\n", static_cast<unsigned int>(wm_info.info.x11.window));
    fflush(stdout);
}

/**
 * @brief Get satellite data
 * 
 * Receives satellite data (orbital speed (km/h) and altitude (km)) from Python and packages
 * them into a vector container to return them for use
 * 
 * @param socket_desc: Integer descriptor of the client socket
 * @param buf: Character buffer array for received data
 * @return Int vector of satellite parameters
 */
std::vector<int> getSatelliteData(int socket_desc, char* buf, std::vector<int> default_params={2, 10}){
    std::vector<int> sat_params; /*Satellite parameters*/
    std::cout << "Inside getSatelliteData\n" << std::flush;

    struct pollfd fds[1];
    fds[0].fd = socket_desc;
    fds[0].events = POLLIN; // Interested in readability events

    while (true)
    {
        int ret = poll(fds, 1, 10); // Wait indefinitely
        if (ret == -1)
        {
            std::cerr << "Error: " << strerror(errno);
            break;
        }
        else if (ret == 0)
        {
            std::cout << "No data available on socket";
            sat_params = default_params;
            break;
        }

        std::cout << "[getSatelliteData] Further tracking..." << std::endl;
        // Check for readability event
        if (fds[0].revents & POLLIN)
        {
            std::cout << "Data available in socket\n" << std::flush;
            // Data is available on socket. Attempt to receive it
            ssize_t bytes_received = recv(socket_desc, buf, sizeof(buf)-1, 0);
            if (bytes_received == -1)
            {
                if (errno == EWOULDBLOCK)
                {
                    std::cerr << "Blocking issue";
                    continue;
                }
                else
                {
                    std::cerr << "Issue with recv";
                    break;
                }
            }
            else if (bytes_received == 0)
            {
                // Sender closed the connection
                std::cout << "Connection closed by peer";
                break;
            }

            // Process the received data
            std::cout << "Received data: " << buf;
            buf[bytes_received] = '\0';

            char* token = strtok(buf, ",");
            int idx = 0;
            while (token){
                sat_params.push_back(atoi(token));
                token = strtok(NULL, ",");
                ++idx;
            }
            return sat_params;
        }
        else
        {
            std::cerr << "No data available on socket, sticking to defaults";
        }
    }

    std::cout << "Returning default values";
    return sat_params;
}


int main()
{
    int angle=0; /*Angular position of satellite*/
    int orbital_speed=2; /*Orbital speed of satellite*/
    int delta_time=10; /*Frameskip time in ms*/
    std::vector<int> sat_params; /*Array of satellite parameters*/
    const char* socket_path = "/tmp/data_socket"; /*Path to the client socket*/
    char buffer[1024]; /*Char buffer for data transmitted through socket*/

    // Create client socket and establish connection request
    int client_socket = createSocket(socket_path);

    // Initialize SDL and create a window and renderer for the window
    SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
    SDL_Window* sim_window = SDL_CreateWindow(
        "Orbit simulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        600, 600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL
    );
    SDL_Renderer* sim_renderer = SDL_CreateRenderer(sim_window, -1, SDL_RENDERER_ACCELERATED);
    
    // Get and send SDL window ID
    captureSDLWindowID(sim_window);

    // SDL event loop
    while(true){
        SDL_Delay(DELAY_MS);
        
        SDL_Event e;
        
        // Quit program if user clicks on "close"
        if(SDL_PollEvent(&e)){
            if (e.type == SDL_QUIT){
                std::cout << "Quitting..." << std::endl;
                break;
            }
        }

        // Produce black window by default
        SDL_SetRenderDrawColor(sim_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sim_renderer);
        
        // Draw Earth (just a blue blob for now, please don't lose your shit over this uwu)
        drawFilledCircle(sim_renderer, 300, 300, 50, {0,0,255,255});

        if (sat_params.empty())
        {
            sat_params = {2,10};
        }
        // Get satellite orbital speed and altitude
        sat_params = getSatelliteData(client_socket, buffer, sat_params);
        // Update position of satellite
        auto [sat_x, sat_y] = calculate_sat_coordinates(angle, sat_params[1]);
        drawFilledCircle(sim_renderer, sat_x, sat_y, 10, {0,255,0,255});
        angle += sat_params[0];

        SDL_RenderPresent(sim_renderer);
    }

    // Cleanup at exit time
    SDL_DestroyRenderer(sim_renderer);
    SDL_DestroyWindow(sim_window);
    close(client_socket);
    unlink(socket_path);
    SDL_Quit();
    return 0;
}