📖 Guía de Comandos SQL
El motor acepta una gramática específica procesada por un parser interno. Aquí los comandos disponibles:

Gestión de Bases de Datos
CREATE DATABASE <nombre>; - Crea un nuevo esquema/carpeta.

SHOW DATABASES; - Lista las bases de datos disponibles.

\\c <nombre>; - Conecta a una base de datos específica.

\\qdb; - Regresa al entorno por defecto (default).

Operaciones con Tablas (DDL)
CREATE TABLE <tabla> (<columnas>); - Soporta tipos como INT, VARCHAR(n), PRIMARY KEY y AUTO_INCREMENT.

SHOW TABLES; - Lista las tablas en la base de datos activa.

DROP TABLE <tabla>; - Elimina la tabla y sus índices asociados.

Manipulación de Datos (DML)
INSERT INTO <tabla> (<columnas>) VALUES (<valores>); - Inserción de registros.

SELECT * FROM <tabla>; - Escaneo completo de tabla.

SELECT * FROM <tabla> WHERE id = <N>; - Búsqueda rápida optimizada por índice B+ Tree.

SELECT * FROM <t1> JOIN <t2>; - Operación de unión entre tablas por ID.

UPDATE <tabla> SET <col> = <val> WHERE id = <N>; - Actualización de registros existentes (requiere espacios exactos en el comando).

DELETE FROM <tabla> WHERE id = <N>; - Eliminación de registros específicos.

Transacciones
BEGIN; - Inicia una transacción global y un snapshot MVCC.

COMMIT; - Aplica los cambios permanentemente y limpia buffers.

ROLLBACK; - Deshace los cambios de la transacción actual.

Otros
HELP;, \\h; o \\?; - Muestra el manual de ayuda detallado.

EXIT; - Cierra la conexión del socket.



2. Ejecutar el Servidor (Motor)
Abre una terminal y lanza el servidor indicando el puerto (asegúrate de que el puerto 5000 esté libre):

Bash
./motor_db 5000


3. Ejecutar el Cliente de Consola
Abre una segunda terminal y conéctate localmente:

Bash
./cliente 127.0.0.1 5000