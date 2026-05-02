# Motor DB en C

acenamiento con páginas de 4096 bytes
- File Manager usando `pread` y `pwrite`
- Buffer Pool básico
- WAL básico
- Transacciones: `BEGIN`, `COMMIT`, `ROLLBACK`
- Recuperación básica al iniciar
- Control de concurrencia con `pthread_mutex`
- Catálogo de tablas
- Parser/Executor SQL básico
- Módulo NL2SQL simulado
- Servidor TCP básico opcional

## para salir de la base de datos al general es 
\qdb

## Compilar

```bash
mkdir build
cd build
cmake ..
make
```

## Ejecutar modo consola

```bash
./motor_db
```

## Comandos disponibles

```sql


CREATE TABLE medico (id INT AUTO_INCREMENT PRIMARY KEY, nombre VARCHAR(100));
INSERT INTO medico (nombre) VALUES ('Roberto');
SELECT * FROM medico;




CREATE TABLE usuarios;
INSERT INTO usuarios VALUES Ana;
SELECT * FROM usuarios;
BEGIN;
INSERT INTO usuarios VALUES Luis;
COMMIT;
BEGIN;
INSERT INTO usuarios VALUES Pedro;
ROLLBACK;
NL muéstrame todos los usuarios
EXIT;
```sql

CREATE TABLE usuarios;
INSERT INTO usuarios VALUES Ana;
INSERT INTO usuarios VALUES Luis;
BEGIN;
INSERT INTO usuarios VALUES Maria;
ROLLBACK;
SELECT * FROM usuarios;
BEGIN;
INSERT INTO usuarios VALUES Carlos;
COMMIT;
SELECT * FROM usuarios;

Resultado correcto final:

Ana
Luis
Carlos

## Ejecutar servidor TCP

make clean
make

```bash
./motor_db --server 5555
```

Desde otra terminal:

```bash
nc localhost 5555
```

## Estructura

- `storage/`: páginas y manejo de archivos
- `buffer/`: buffer pool
- `transaction/`: transacciones, WAL y recuperación
- `concurrency/`: bloqueos
- `query/`: parser y executor
- `catalog/`: metadatos
- `nl/`: lenguaje natural a SQL
- `network/`: servidor TCP
- `utils/`: logger


## PARA VER LOS USUARIO EN LA TERMINAL SE DEBE ENTRAR A LA MISMA CARPETA motor_db y colocar esto
##  strings data/usuarios.tbl

# esto mostrara los datos de los usuarios.tbl