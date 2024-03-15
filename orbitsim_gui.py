# imports
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QPushButton, 
                             QHBoxLayout, QSizePolicy, QLabel, QLineEdit)
from PyQt6.QtCore import QProcess, QEventLoop
from PyQt6.QtGui import QWindow
import os
import socket
import sys
from pathlib import Path
import logging
import colorlog

class CustomFormatter(colorlog.ColoredFormatter):
    """Class for formatting logging messages

    Args:
        colorlog (_type_): Sets the colors and format of the log messages
    """
    def __init__(self):
        super().__init__(
            "%(asctime)s - %(name)s - %(log_color)s%(levelname)s - %(message)s (%(filename)s:%(lineno)d)",
            datefmt=None,
            reset=True,
            log_colors={
                'DEBUG': 'cyan',
                'INFO': 'green',
                'WARNING': 'yellow',
                'ERROR': 'red',
                'CRITICAL': 'bold_red',
            },
            secondary_log_colors={},
            style='%'
        )
    
# Init logging 
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

handler = logging.StreamHandler()
handler.setLevel(logging.DEBUG)

handler.setFormatter(CustomFormatter())
logger.addHandler(handler)

# Obtain path of parent directory
CURRENT_DIRECTORY = Path(__file__).resolve().parent

# PyQt6 GUI window class
class SimWindow(QMainWindow):
    """The class that creates the satellite operation GUI

    Args:
        QMainWindow (QMainWindow): PyQt6 QMainWindow
    """
    def __init__(self):
        """Constructs the GUI and its elements
        """
        super().__init__()

        # True if the SDL2 window ID is captured by Python
        # This bool variable is to prevent processing regular messages as if they were numbers
        self.SDL_id_found = False   
        self.default_vals = True    # If true, default satellite params are sent to C++

        # Create socket and establish connection first
        self.create_socket()
        self.sat_connection = None  # Socket connection object

        # Create vertical layout for all the stuff
        self.main_widget = QWidget()
        layout = QVBoxLayout(self.main_widget)

        # Orbital speed 
        self.orbital_speed_layout = QHBoxLayout()
        self.orbital_speed_label = QLabel("Orbital speed (km/h):")
        self.orbital_speed = QLineEdit(self.main_widget)
        self.orbital_speed.setPlaceholderText("10")
        self.orbital_speed_layout.addWidget(self.orbital_speed_label)
        self.orbital_speed_layout.addWidget(self.orbital_speed)

        # Altitude 
        self.altitude_layout = QHBoxLayout()
        self.altitude_label = QLabel("Altitude (km):")
        self.altitude = QLineEdit(self.main_widget)
        self.altitude.setPlaceholderText("10")
        self.altitude_layout.addWidget(self.altitude_label)
        self.altitude_layout.addWidget(self.altitude)

        # Send values to sim
        self.send_params = QPushButton("Apply")
        self.send_params.clicked.connect(self.send_data)

        layout.addLayout(self.orbital_speed_layout)
        layout.addLayout(self.altitude_layout)
        layout.addWidget(self.send_params)
        
        win_id = self.find_id(os.fspath(CURRENT_DIRECTORY/"sdl_orbitsim"))
        window = QWindow.fromWinId(win_id)
        widget = QWidget.createWindowContainer(window)
        widget.setFixedSize(600, 600)
        widget.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        layout.addWidget(widget)

        self.process.readyReadStandardError.connect(self.handle_stderr)

        self.main_widget.setFixedSize(400, 200)
        self.setCentralWidget(self.main_widget)

    def create_socket(self):
        """Creates a Unix domain non-blocking socket and begins listening to 
        client connection requests
        """
        self.socket_path = "/tmp/data_socket"
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)

        logger.info(f"Creating socket path in {self.socket_path}")

        self.sat_server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sat_server.setblocking(0)
        self.sat_server.bind(self.socket_path)
        self.sat_server.listen(1)
        
        logger.info("Socket created. Listening for incoming connections")


    def send_data(self):
        """Sends satellite parameters to C++
        """
        orbital_speed = self.orbital_speed.text()
        altitude = self.altitude.text()
        logger.debug(f"Orbital speed: {orbital_speed} km/h -- Altitude: {altitude} km")
        sat_data = f"{orbital_speed}, {altitude}"

        try:
            if self.sat_connection is None or self.sat_connection.fileno() < 0:
                self.sat_connection, self.sat_clientaddress = self.sat_server.accept()
                logger.info("Accepted new connection")
            
            self.sat_connection.sendall(sat_data.encode())
            logger.debug("Sent data to sdl_orbitsim.cpp")
        except (IOError, UnicodeDecodeError) as e:
            logger.error(f"Error sending data: {e}")
            self.sat_connection = None
        
    def handle_stderr(self):
        error_data = self.process.readAllStandardError()
        error_data = error_data.data().decode()
        logger.error(f"[C++] {error_data}")

    def find_id(self, exe):
        """Captures the SDL2 window ID for PyQt6 to render the window 

        Args:
            exe (os.PathLike): The file path to the C++ executable

        Returns:
            int: The SDL2 window ID
        """
        logger.debug("Finding SDL window ID...")
        win_id = -1
        self.process = QProcess()
        self.process.setProgram(exe)
        loop = QEventLoop()

        def handle_readyReadStandardOutput():
            text = self.process.readAllStandardOutput().data().decode()
            if not self.SDL_id_found:
                if "ID" in text:
                    _, id_str = text.split()
                    nonlocal win_id
                    win_id = int(id_str)
                    logger.debug(f"SDL Window ID: {id_str}")
                    loop.quit()
                    self.SDL_id_found = True
                else:
                    logger.info(text)
            else:
                logger.info(text)

        self.process.readyReadStandardOutput.connect(handle_readyReadStandardOutput)
        self.process.start()
        loop.exec()
        return win_id

if __name__ == "__main__":
    logger.info("Starting program")
    app = QApplication([])
    window = SimWindow()
    window.show()
    sys.exit(app.exec())
    