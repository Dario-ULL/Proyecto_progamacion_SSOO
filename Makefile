# Nombre del ejecutable
TARGET = docserver

# Archivos fuente
SRC = docserver.cc

# Compilador y banderas
CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17

# Regla principal (por defecto)
all: $(TARGET)

# Regla para compilar el ejecutable
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Regla para limpiar archivos generados
clean:
	rm -f $(TARGET)

# Regla para reconstruir todo desde cero
rebuild: clean all
