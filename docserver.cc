#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cerrno>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <optional>
#include <limits.h> 
#include <algorithm>

#include "SafeMap.h"

using SafeFD = int;  

/**
 * Enumeración para los códigos de error
 */
enum ErrorCode {
  SUCCESS = 0,
  ERROR_ARGUMENTOS = 1,
  ERROR_OPCION_DESCONOCIDA = 2,
  ERROR_SISTEMA = 3,
  ERROR_PERMISOS = 4,
  ERROR_NO_ENCONTRADO = 5,
  ERROR_NO_SE_PUEDE_OBTENER_TAMANO = 6,
  ERROR_NO_ENCONTRADO_SOCKET = 7,
  ERROR_SOCKET_NO_ESCUCHANDO = 8,
  ERROR_LEVE_AL_ENVIAR = 9,
  ERROR_AL_ENVIAR = 10,
  ERROR_VARIABLE_NO_DEFINIDA = 11,
  ERROR_PETICION_VACIA = 12,
  ERROR_AL_ABRIR_ARCHIVO = 13,
  ERROR_AL_MAPEAR_ARCHIVO = 14,
  ERROR_AL_OBTENER_EL_TAMAÑO_ARCHIVO = 15
};

/**
 * Estructura para los argumentos del programa
 */
struct Args {
  bool verbose = false;
  bool help = false;
  std::string archivo;
  std::string directorio;
  int puerto = 8080; // Puerto predeterminado
};

/**
 * Función para mostrar la ayuda del programa
 * Muestra la ayuda del programa con las opciones disponibles y cómo se deben utilizar.
 * @return void
 */
void mostrarAyuda() {
  std::cout << "Uso: docserver [-v | --verbose] [-h | --help] [-p <puerto> | --port <puerto>] ARCHIVO\n"
            << "Opciones:\n"
            << "  -v, --verbose   Muestra información adicional sobre las funciones utilizadas\n"
            << "  -h, --help      Muestra esta ayuda y termina\n"
            << "  -p, --port      Puerto en el que el servidor escuchará las conexiones\n"
            << "  -b, --base      Directorio base donde buscar archivos solicitados\n";
}

/**
 * Función para obtener el valor de una variable de entorno
 * @param name Nombre de la variable de entorno
 * @return Valor de la variable de entorno
 */
std::string getenv(const std::string& name) {
  const char* value = std::getenv(name.c_str());
  if (value == nullptr) {
    return "";  // Devuelve una cadena vacía si la variable no está definida
  } else {
    return std::string(value);  // Devuelve el valor de la variable de entorno
  }
}

/**
 * Función para verificar si una cadena es una dirección o solo una palabra
 * @param str Cadena a verificar
 * @return true si la cadena es una dirección, false si no lo es
 */
bool es_direccion(std::string& str) {
    // Eliminar 'GET' y los posibles espacios que lo acompañen al inicio
    const std::string GET_prefix = "GET ";
    if (str.find(GET_prefix) == 0) {
        str.erase(0, GET_prefix.length()); // Eliminar 'GET ' del inicio
    }

    // Verificamos si la cadena contiene una barra (usada comúnmente en rutas de archivo)
    for (char c : str) {
        if (c == '/' || c == '\\') {
            return true; // Si contiene una barra, es una dirección
        }
    }
    return false; // Si no contiene barras, es solo una palabra
}

/**
 * Función para parsear los argumentos del programa
 * @param argc Cantidad de argumentos
 * @param argv Arreglo de argumentos
 * @param args Estructura para almacenar los argumentos
 * @return Código de error
 */
ErrorCode parse_args(int argc, char* argv[], Args& args) {
  if (argc < 1) {
    return ERROR_ARGUMENTOS;
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-v" || arg == "--verbose") {
      args.verbose = true;
    } else if (arg == "-h" || arg == "--help") {
      args.help = true;
    } else if (arg == "-p" || arg == "--port") {
      if (i + 1 < argc) {
        args.puerto = std::stoi(argv[++i]);
      } else {
        std::cerr << "Error: Opción --port requiere un valor.\n";
        return ERROR_ARGUMENTOS;
      }
    } else if (arg == "-b" || arg == "--base") {
      if (i + 1 < argc) {
        args.directorio = std::string(argv[++i]);
      } else {
        const char* env_dir = getenv("DOCSERVER_BASEDIR");
        if (env_dir) {
          std::cout << "Direccion especificada en DOCSERVER_BASEDIR\n";
          try {
            args.directorio = env_dir; 
          } catch (const std::exception& e) {
            std::cerr << "Error: El valor de BASEDIR no es válido.\n";
            return ERROR_VARIABLE_NO_DEFINIDA;
          }
        } else {
          std::cout << "Direccion no especificada en DOCSERVER_BASEDIR\n";
        }
      }
    } else {
      std::cerr << "Error: Opción no reconocida \"" << arg << "\".\n";
      return ERROR_OPCION_DESCONOCIDA;
    }
  }

  return SUCCESS;
}

/**
 * Función para leer la petición del cliente
 * @param clientSocket Socket del cliente
 * @param archivo Referencia a un string donde se almacenará la petición
 * @return Código de error
 */
ErrorCode readClientRequest(int clientSocket, std::string& archivo) {
    char buffer[1024];
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead <= 0) {
        if (bytesRead == 0) {
            std::cerr << "Conexión cerrada por el cliente" << std::endl;
        } else if (errno == ECONNRESET) {
            std::cerr << "Conexión reiniciada por el cliente" << std::endl;
        } else {
            std::cerr << "Error al recibir la petición del cliente" << std::endl;
        }
        return ERROR_PETICION_VACIA;
    }

    buffer[bytesRead] = '\0'; // Asegurarse de que el string esté terminado
    archivo = buffer;

    // Verificar que la petición no esté vacía
    if (archivo.empty()) {
        return ERROR_PETICION_VACIA;
    }

    return SUCCESS;
}

/**
 * Función para leer un archivo y mapearlo en memoria
 * @param path Ruta del archivo a leer
 * @return Par con un SafeMap y un código de error
 */
std::pair<SafeMap, ErrorCode> read_file(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        return {SafeMap("", nullptr, 0), ERROR_AL_ABRIR_ARCHIVO};
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return {SafeMap("", nullptr, 0), ERROR_AL_OBTENER_EL_TAMAÑO_ARCHIVO}; 
    }
    size_t size = st.st_size;

    void* mapped_memory = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped_memory == MAP_FAILED) {
        close(fd);
        return {SafeMap("", nullptr, 0), ERROR_AL_MAPEAR_ARCHIVO}; 
    }

    close(fd);

    std::string_view sv(reinterpret_cast<const char*>(mapped_memory), size);
    return {SafeMap(sv, mapped_memory, size), SUCCESS}; 
}

/**
 * Función para enviar una respuesta al cliente
 * @param socket Socket del cliente
 * @param header Cabecera de la respuesta
 * @param body Cuerpo de la respuesta
 * @param args Argumentos del programa
 * @return Código de error
 */
ErrorCode send_response(const SafeFD& socket, std::string_view header, Args& args, std::string_view body = {}) {
  std::string response = std::string(header) + "\n";
  if (args.verbose) {
    std::string open = "open: se abre el archivo \"" + args.archivo + "\"\n";
    send(socket, open.c_str(), open.size(), 0);
  }
  ssize_t sent_bytes = send(socket, response.c_str(), response.size(), 0);
  if (sent_bytes == -1) {
    if (errno == ECONNRESET) {
      std::cerr << "Error leve: la conexión fue restablecida (ECONNRESET), cerrando la conexión.\n";
      return ERROR_LEVE_AL_ENVIAR;
    } else {
      std::cerr << "Error al enviar la cabecera: " << strerror(errno) << std::endl;
      return ERROR_AL_ENVIAR; 
    }
  }

  size_t offset = 0;
  size_t body_size = body.size();
  const size_t chunk_size = 1024; 

  while (offset < body_size) {
    size_t remaining = body_size - offset;
    size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;

    ssize_t chunk_sent = send(socket, body.data() + offset, to_send, 0);
    if (chunk_sent == -1) {
      std::cerr << "Error al enviar el cuerpo: " << strerror(errno) << std::endl;
      return ERROR_AL_ENVIAR;
    }

    offset += chunk_sent;
  }

  if (args.verbose) {
    std::string close = "\nread: se leen " + std::to_string(body.size()) + " bytes de \"" + args.archivo + "\"\n" + "close: se cierra el archivo \"" + args.archivo + "\"\n";
    send(socket, close.c_str(), close.size(), 0);
  }
  return SUCCESS;
}

/**
 * Función para manejar la conexión con un cliente
 * @param client_socket Socket del cliente
 * @param args Argumentos del programa
 * @return Código de error
 */
ErrorCode handle_client(int client_socket, Args& args) {
  while (true) {
    ErrorCode error = readClientRequest(client_socket, args.archivo);
    if (error != SUCCESS) {
      close(client_socket);
      return error;
    }

    args.archivo.erase(std::remove(args.archivo.begin(), args.archivo.end(), '\n'), args.archivo.end());

    if (args.archivo == "close") {
      std::cout << "Cerrando la conexión con el cliente." << std::endl;
      close(client_socket);
      return SUCCESS;
    }

    if (args.archivo.empty()) {
      send_response(client_socket, "400 Bad Request\n", args);
      close(client_socket);
      return ERROR_ARGUMENTOS;
    }

    std::string content;
    std::string error_message = "HTTP/1.1 404 Not Found\n";

    if (es_direccion(args.archivo)) {
      auto [safeMap, error] = read_file(args.archivo);
      if (error != SUCCESS) {
        send_response(client_socket, error_message, args, content);
        close(client_socket);
        return error;
      }
      content = safeMap.get();
      std::cout << "Archivo solicitado: " << args.archivo << std::endl;
    } else {
      auto [safeMap, error] = read_file(args.directorio + "/" + args.archivo);
      if (error != SUCCESS) {
        send_response(client_socket, error_message, args, content);
        close(client_socket);
        return error;
      }
      content = safeMap.get();
      std::cout << "Archivo solicitado: " << args.directorio + "/" + args.archivo << std::endl;
    }
    std::string header;
    if (args.verbose) {
      header = "HTTP/1.1 200 OK\n";
    } else {
      header = "HTTP/1.1 200 OK\nContent-Length: " + std::to_string(content.size()) + "\n";
    }
    send_response(client_socket, header, args, content + "\n");
  }

  close(client_socket);
  return SUCCESS;
}

/**
 * Función para aceptar una conexión
 * @param server_socket Socket del servidor
 * @param client_addr Dirección del cliente
 * @return Socket del cliente
 */
std::optional<SafeFD> accept_connection(const SafeFD& server_socket, sockaddr_in& client_addr) {
  socklen_t client_addr_len = sizeof(client_addr);

  SafeFD client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);

  if (client_socket == -1) {
    std::cerr << "Error al aceptar la conexión: " << strerror(errno) << std::endl;
    return std::nullopt;  // Si hay un error, retornamos nullopt
  }

  // Si la opción verbose está activada, mostramos la dirección del cliente
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
  std::cout << "Cliente conectado desde: " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

  return client_socket;  
}

/**
 * Función para crear un socket
 * @param port Puerto del socket
 * @return Socket creado
 */
std::optional<SafeFD> make_socket(uint16_t port) {
  SafeFD server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    std::cerr << "Error al crear el socket: " << strerror(errno) << std::endl;
    return std::nullopt;  // Usar std::nullopt para indicar que no se pudo crear el socket
  }

  sockaddr_in server_addr{}; 
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
    std::cerr << "Error al bindear el socket: " << strerror(errno) << std::endl;
    close(server_fd);
    return std::nullopt;  // Indicar que no se pudo bindear el socket
  }

  return server_fd;
}

/**
 * Función para poner el socket a la escucha
 * @param server_fd Socket del servidor
 * @return Código de error
 */
ErrorCode listen_connection(const SafeFD& server_fd) {
  if (listen(server_fd, 10) == -1) {
    return ERROR_SOCKET_NO_ESCUCHANDO;  // Devolver el valor de errno en caso de error
  }
  return SUCCESS;  
}

/**
 * Función para iniciar el servidor
 * @param args Argumentos del programa
 * @return Código de error
 */
ErrorCode start_server(Args& args) {
  auto socket_result = make_socket(args.puerto); 
  if (socket_result) {
    SafeFD server_fd = *socket_result;

    // Poner el socket a la escucha
    int result = listen_connection(server_fd);
    if (result != 0) {
      std::cerr << "Error al poner el socket a la escucha, código de error: " << result << std::endl;
      close(server_fd);
      return ERROR_NO_ENCONTRADO_SOCKET;
    }

    std::cout << "Servidor escuchando en el puerto " << args.puerto << "...\n";

    sockaddr_in client_addr;
    while (true) {
      auto client_socket = accept_connection(server_fd, client_addr);
      if (!client_socket) {
        continue;
      }
      ErrorCode error = handle_client(*client_socket, args);
      if (error != SUCCESS) {
        close(*client_socket);
        return error;
      }
    }

    close(server_fd);
  } else {
    std::cerr << "Fallo al crear el socket.\n";
    return ERROR_NO_ENCONTRADO_SOCKET;
  }
  return SUCCESS;
}

int main(int argc, char* argv[]) {
  Args args;
  // Parsear los argumentos

  char currentPath[PATH_MAX];
  if (getcwd(currentPath, sizeof(currentPath)) == nullptr) {
      perror("Error obteniendo el directorio actual");
  }
  args.directorio = currentPath;
  
  const char* env_port = getenv("DOCSERVER_PORT");
  if (env_port) {
    try {
      args.puerto = std::stoi(env_port); 
    } catch (const std::exception& e) {
      std::cerr << "Error: El valor de PORT no es válido.\n";
      return ERROR_VARIABLE_NO_DEFINIDA;
    }
  } else {
    std::cout << "Puerto no especificado en DOCSERVER_PORT\n";
  }

  auto error_code = parse_args(argc, argv, args);


  
  std::cout << "Directorio base: " << args.directorio << std::endl;
 
  if (error_code != SUCCESS) {
    switch (error_code) {
      case ERROR_ARGUMENTOS:
        std::cerr << "Uso incorrecto de los argumentos.\n";
        break;
      case ERROR_OPCION_DESCONOCIDA:
        std::cerr << "Opción desconocida.\n";
        break;
      default:
        std::cerr << "Error desconocido.\n";
        break;
    }
    return error_code;
  }

  if (args.help) {
    mostrarAyuda();
    return SUCCESS;
  }

  ErrorCode error = start_server(args);

  if (error == ERROR_PERMISOS) {
    std::cerr << "Error: No se tienen permisos para leer el archivo \"" << args.archivo << "\".\n";
    std::cout << "403 Forbidden\n";
  } else if (error == ERROR_NO_ENCONTRADO) {
    std::cerr << "Error: El archivo \"" << args.archivo << "\" no existe.\n";
    std::cout << "404 Not Found\n";
  } else if (error == ERROR_NO_SE_PUEDE_OBTENER_TAMANO) {
    std::cerr << "Error: No se pudo obtener el tamaño del archivo \"" << args.archivo << "\".\n";
    std::cout << "500 Internal Server Error\n";
  } else if (error == ERROR_NO_ENCONTRADO_SOCKET) {
    std::cerr << "Error: No se pudo encontrar o crear el socket.\n";
    std::cout << "501 Internal Server Error\n";
  } else if (error == ERROR_SOCKET_NO_ESCUCHANDO) {
    std::cerr << "Error: El socket no está escuchando.\n";
    std::cout << "502 Internal Server Error\n";
  } else if (error == ERROR_LEVE_AL_ENVIAR) {
    std::cerr << "Error: Ocurrió un error leve al enviar la respuesta.\n";
    std::cout << "503 Internal Server Error\n";
  } else if (error == ERROR_AL_ENVIAR) {
    std::cerr << "Error: Ocurrió un error fatal al enviar la respuesta.\n";
    std::cout << "504 Internal Server Error\n";
  } else if (error == ERROR_VARIABLE_NO_DEFINIDA) {
    std::cerr << "Error: La variable no está definida.\n";
    std::cout << "400 Bad Request\n";
  } else if (error == ERROR_PETICION_VACIA) {
    std::cerr << "Error: La petición está vacía.\n";
    std::cout << "400 Bad Request\n";
  } else if (error == ERROR_AL_ABRIR_ARCHIVO) {
    std::cerr << "Error: No se pudo abrir el archivo \"" << args.archivo << "\".\n";
    std::cout << "505 Internal Server Error\n";
  } else if (error == ERROR_AL_MAPEAR_ARCHIVO) {
    std::cerr << "Error: Ocurrió un error al mapear el archivo \"" << args.archivo << "\".\n";
    std::cout << "506 Internal Server Error\n";
  } else if (error == ERROR_AL_OBTENER_EL_TAMAÑO_ARCHIVO) {
    std::cerr << "Error: No se pudo obtener el tamaño del archivo \"" << args.archivo << "\".\n";
    std::cout << "506 Internal Server Error\n";
  } else if (error != SUCCESS) {
    std::cerr << "Error: Ocurrió un problema al procesar el archivo \"" << args.archivo << "\".\n";
    std::cout << "507 Internal Server Error\n";
  } else {
    std::cout << "Archivo procesado correctamente.\n";
  }

  return SUCCESS;
}
