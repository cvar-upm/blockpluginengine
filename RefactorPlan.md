# Plan de cambios — Blackboard extendida y RosBoundaryBlock genérico

## 1. Objetivo

Extender Block Plugin Engine (BPE) para permitir bloques de frontera ROS 2 genéricos capaces de publicar y suscribirse a tipos ROS determinados en tiempo de configuración, sin necesidad de implementar un plugin específico para cada tipo de mensaje.

El diseño debe mantener las propiedades fundamentales actuales:

- La Blackboard continúa almacenando objetos nativos `T`.
- Los plugins internos continúan utilizando la API tipada normal de la Blackboard.
- El tipo se borra internamente después de crear los objetos.
- Los objetos `T` de los slots se crean durante la configuración y se reutilizan durante toda la ejecución.
- Un slot representa **una única muestra**, aunque pueda contener simultáneamente una representación nativa y una representación serializada.
- La adquisición y el `commit` continúan siendo operaciones únicas sobre la muestra completa.
- Un publisher ROS es un **consumer** de la Blackboard.
- Un subscriber ROS es un **producer** de la Blackboard.
- ROS 2 sigue estando restringido a los bloques de frontera.
- La primera versión puede utilizar memoria dinámica en la representación serializada.
- La eliminación de asignaciones dinámicas durante ejecución se abordará posteriormente como una segunda fase.

---

# 2. Modelo conceptual del slot BPE

La Blackboard original continúa teniendo conceptualmente:

```text
Slot
└── native data: shared_ptr<void>
```

La Blackboard extendida por BPE debe disponer de:

```text
BPE Slot
├── native data: shared_ptr<void>
└── serialized data: SerializedMessage
```

Ambas representaciones pertenecen a **la misma muestra**.

Por tanto, no deben existir:

- un `commit` para el objeto nativo y otro para el serializado;
- dos secuencias independientes;
- dos mecanismos de reserva;
- dos estados de publicación.

El estado de sincronización del slot es único:

```text
claim
  │
  ├── native representation
  └── serialized representation
  │
commit
```

y, simétricamente:

```text
read/acquire
  │
  ├── native representation
  └── serialized representation
  │
release
```

---

# 3. Cambios mínimos en `lfbb_blackboard`

`lfbb_blackboard` no debe adquirir ninguna dependencia de ROS 2 ni conocer el concepto de mensaje serializado.

Los cambios en `lfbb` deben limitarse a permitir que una librería derivada pueda especializar el almacenamiento y el comportamiento asociado a la creación de los slots.

## 3.1. Preparar las clases necesarias para herencia

Revisar las clases implicadas en:

- `Blackboard`;
- `SharedData`;
- `SD_RingSlot`;
- factories;
- grants.

Modificar únicamente la visibilidad y/o virtualidad necesaria para que BPE pueda proporcionar implementaciones derivadas.

En particular, revisar:

- destructores que necesiten ser virtuales;
- miembros actualmente `private` que deban pasar a `protected`;
- métodos de creación/inicialización que necesiten un punto de extensión;
- acceso al slot desde grants derivados.

No introducir conceptos BPE o ROS en `lfbb`.

## 3.2. Mantener intacta la API pública normal

La API existente:

```cpp
register_producer<T>()
register_consumer<T>()
ProducerGrant<T>
ConsumerGrant<T>
```

debe continuar funcionando como hasta ahora.

Los usuarios normales de `lfbb_blackboard` no deben necesitar conocer las extensiones introducidas para BPE.

## 3.3. Mantener la creación tipada de los payloads

Los objetos almacenados en los slots deben continuar siendo creados desde código que conoce `T`:

```cpp
std::make_shared<T>()
```

No introducir construcción type-erased mediante:

```text
size_of
placement new
MessageMembers::init_function
MessageMembers::fini_function
```

La introspection de ROS 2 **no es necesaria para construir los objetos almacenados en la Blackboard**.

---

# 4. Blackboard extendida de BPE

Crear una extensión de `Blackboard` específica de BPE.

Nombre definitivo pendiente de elegir durante la implementación; conceptualmente:

```text
Blackboard
    ▲
    │
BPEBlackboard
```

Esta clase añade exclusivamente las capacidades necesarias para los bloques de frontera.

## 4.1. Slot extendido

Crear un slot BPE derivado o especializado a partir del slot de `lfbb`.

Conceptualmente:

```text
SD_RingSlot
      ▲
      │
BPE_RingSlot
      │
      ├── native data
      └── serialized data
```

La representación serializada debe tener exactamente el mismo lifetime lógico que la representación nativa de la muestra.

## 4.2. Entrada inicialmente no materializada

Debe ser posible registrar primero un boundary genérico.

En ese caso:

```text
Generic boundary
      │
      ▼
Entry
      │
      ├── ring creado/dimensionado
      └── payloads == nullptr
```

Todavía no se conoce `T` estáticamente y, por tanto, no se construyen objetos.

La entrada queda en estado conceptualmente:

```text
UNMATERIALIZED
```

## 4.3. Materialización por el primer participante tipado

Cuando posteriormente aparezca:

```cpp
register_producer<T>()
```

o:

```cpp
register_consumer<T>()
```

la entrada se materializa.

Para cada slot:

```cpp
std::make_shared<T>()
```

La transición es única:

```text
UNMATERIALIZED
       │
       │ first typed participant<T>
       ▼
MATERIALIZED<T>
```

Una vez materializada, no vuelve al estado anterior.

## 4.4. Entrada creada inicialmente por participante tipado

Si el primer participante es tipado, la entrada puede crearse directamente materializada, conservando esencialmente el comportamiento actual de `lfbb`.

Por tanto deben ser equivalentes:

```text
typed → generic
```

y:

```text
generic → typed
```

al finalizar la configuración.

---

# 5. Dimensionamiento preventivo del ring

Debe evitarse que sea necesario crear nuevos slots después de haber perdido el conocimiento estático de `T`.

Para ello, BPE utilizará un margen adicional al dimensionar inicialmente el ring.

La Blackboard actual redondea el número necesario de slots a una potencia de dos.

BPE debe calcular la capacidad considerando los posibles participantes genéricos que todavía no se hayan registrado.

## 5.1. Regla inicial

Si todavía no se ha registrado ningún boundary genérico:

```text
margen = 4
```

Esto cubre:

```text
margen normal existente:       2
posible publisher posterior:   1
posible subscriber posterior:  1
                              ---
                               4
```

## 5.2. Si ya existe un boundary genérico

Si uno de los dos posibles participantes genéricos ya está registrado cuando se materializa el ring:

```text
margen = 3
```

No es necesario conservar posteriormente información específica para gestionar crecimiento.

## 5.3. Redondeo

Se conserva la política actual de potencia de dos:

```text
required <= 2^p
```

El pequeño sobredimensionamiento adicional se considera preferible a introducir:

- factories type-erased;
- constructores type-erased;
- metadata de construcción;
- crecimiento tardío de payloads.

---

# 6. Eliminación de entradas sin participante tipado

Después de registrar/configurar todos los bloques, una entrada que permanezca:

```text
UNMATERIALIZED
```

significa que sólo existen participantes genéricos.

Conceptualmente correspondería a algo como:

```text
ROS input → Blackboard → ROS output
```

sin ningún procesamiento interno tipado.

Ese caso no forma parte del propósito de BPE.

Por tanto, durante la finalización de la configuración:

```text
for each Entry:
    if UNMATERIALIZED:
        remove Entry
```

Puede emitirse un warning diagnóstico, pero **no debe considerarse un error de configuración**.

El warning puede decidirse posteriormente como detalle de logging.

---

# 7. Identidad y validación del tipo ROS

Las entradas utilizadas por boundaries deben conservar suficiente información para identificar el tipo ROS configurado.

Por ejemplo, conceptualmente:

```text
ROS type:
    sensor_msgs/msg/Imu
```

y el correspondiente type support.

Cuando aparece el primer participante tipado `T`, debe verificarse que ese `T` corresponde al tipo ROS declarado por el boundary.

La API exacta utilizada para realizar esta comprobación debe estudiarse durante la implementación.

La propiedad que debe garantizarse es:

```text
runtime ROS type == typed participant T
```

antes de materializar la entrada.

No debe basarse simplemente en comparar directamente:

```cpp
std::type_index(typeid(T))
```

con un nombre ROS, ya que representan sistemas de identificación de tipos distintos.

La solución debe encapsular este puente dentro de BPE.

---

# 8. Grants BPE

BPE necesita variantes o extensiones de los grants que permitan a los boundaries trabajar sin conocer `T`.

Deben conservarse las mismas garantías RAII de los grants actuales.

## 8.1. Producer grant genérico

Necesario principalmente para el subscriber ROS.

Debe permitir acceder a:

```text
native payload → void *
serialized payload
```

del mismo slot reservado.

Conceptualmente:

```text
GenericProducerGrant
├── native_data()       → void *
├── serialized_data()
└── commit()
```

No debe existir un segundo mecanismo de commit para la representación serializada.

## 8.2. Consumer grant genérico

Necesario principalmente para el publisher ROS.

Debe permitir acceder a la representación serializada del slot publicado.

Conceptualmente:

```text
GenericConsumerGrant
├── serialized_data()
├── status
├── sequence
├── timestamp
└── demás metadata existente
```

Debe mantener la reserva del reader exactamente igual que `ConsumerGrant<T>`.

## 8.3. API tipada existente

Los grants tipados continúan proporcionando:

```text
ProducerGrant<T>
ConsumerGrant<T>
```

sin obligar a los algoritmos internos a conocer la representación serializada.

---

# 9. Publisher ROS genérico

El publisher ROS es un **consumer de la Blackboard**.

No debe serializar.

La serialización debe realizarse antes del `commit`, por el producer tipado que genera la muestra.

Flujo:

```text
Algorithm<T>
     │
     ▼
ProducerGrant<T>
     │
     ├── write T
     │
     ├── serialize T
     │
     ▼
single commit
     │
     ▼
slot
├── native T
└── serialized message
     │
     ▼
GenericConsumerGrant
     │
     ▼
RosBoundaryBlock
     │
     ▼
GenericPublisher
```

El publisher sólo consume la representación serializada ya existente.

---

# 10. Serialización en el producer tipado

Cuando una entrada tiene asociado un output ROS, el producer debe generar ambas representaciones antes del commit:

```text
T
│
├── permanece como native payload
│
└── serialize
      │
      ▼
SerializedMessage
```

Después:

```text
commit(native + serialized)
```

La serialización sólo debe realizarse cuando exista realmente un boundary que la necesite.

Una entrada utilizada exclusivamente por algoritmos internos no debe pagar el coste de serialización.

La extensión BPE debe encapsular esta decisión para que el algoritmo productor no tenga que gestionar explícitamente ROS.

---

# 11. Subscriber ROS genérico

El subscriber ROS es un **producer de la Blackboard**.

Recibe un mensaje serializado mediante:

```text
GenericSubscription
```

y debe producir simultáneamente las dos representaciones del slot.

Flujo:

```text
GenericSubscription
        │
        ▼
SerializedMessage
        │
        ▼
GenericProducerGrant
        │
        ├───────────────┐
        │               │
        ▼               ▼
serialized copy/     native void *
ownership               │
                        ▼
                 deserialize
                        │
                        ▼
                     native T
        │               │
        └───────┬───────┘
                ▼
           single commit
```

---

# 12. Deserialización type-erased

La deserialización no necesita conocer `T` estáticamente.

ROS 2 proporciona la operación necesaria mediante el type support:

```text
SerializedMessage
       +
ROS type support
       +
void * destination
       │
       ▼
deserialize
```

El `void *` apunta al objeto `T` que ya existe en el slot.

Por tanto:

- el subscriber no construye `T`;
- no destruye `T`;
- no necesita conocer `sizeof(T)`;
- no necesita introspection para crear el objeto;
- sólo modifica un objeto previamente materializado.

La operación debe encapsularse en BPE para que `RosBoundaryBlock` no dependa directamente de los detalles de `rmw_deserialize()`.

Conceptualmente:

```text
RosRuntimeType
├── type identity
├── type support
├── serialize(...)
└── deserialize(...)
```

La forma exacta de esta clase puede decidirse durante la implementación.

---

# 13. Simetría publisher/subscriber

El resultado debe presentar una simetría clara.

## Output

```text
typed producer
      │
      ├── native T
      └── serialize(T)
      │
      ▼
single commit
      │
      ▼
generic consumer
      │
      ▼
ROS publisher
```

## Input

```text
ROS subscriber
      │
      ▼
generic producer
      │
      ├── serialized
      └── deserialize → native T
      │
      ▼
single commit
      │
      ▼
typed consumer
```

En ambos casos el slot contiene una única muestra con dos representaciones coherentes.

---

# 14. Integración con `RosBoundaryBlock`

`RosBoundaryBlock` debe seguir representando exclusivamente la frontera ROS.

Se conservan las reglas actuales:

```text
RosBoundaryBlock + !TimerBlock
        =
ROS input
```

y:

```text
RosBoundaryBlock + TimerBlock
        =
ROS output
```

## Input

El boundary:

- crea una `GenericSubscription`;
- conoce el tipo ROS configurado;
- registra un producer genérico en la Blackboard;
- al recibir un mensaje obtiene un generic producer grant;
- conserva/transfiere la representación serializada;
- deserializa sobre el objeto nativo ya existente;
- realiza un único commit.

## Output

El boundary:

- crea un `GenericPublisher`;
- conoce el tipo ROS configurado;
- registra un consumer genérico;
- durante `tick()` obtiene el último generic consumer grant;
- publica directamente su representación serializada.

No serializa durante `tick()`.

---

# 15. Orden recomendado de implementación

La implementación debe realizarse incrementalmente.

## Fase 1 — Preparación de `lfbb`

1. Identificar los puntos mínimos de extensión.
2. Preparar `Blackboard`, `SharedData`, slots y grants para herencia/extensión.
3. No introducir ninguna dependencia BPE/ROS.
4. Ejecutar todos los tests actuales de `lfbb`.
5. Confirmar que no cambia su comportamiento público.

## Fase 2 — Blackboard BPE

1. Crear el slot extendido.
2. Crear la Blackboard extendida.
3. Permitir entradas no materializadas.
4. Implementar materialización posterior mediante `T`.
5. Implementar el margen preventivo del ring.
6. Implementar eliminación final de entradas no materializadas.

Todavía sin ROS publisher/subscriber.

## Fase 3 — Grants genéricos

Implementar:

```text
GenericProducerGrant
GenericConsumerGrant
```

y comprobar que:

- utilizan exactamente las mismas reservas que los grants tipados;
- no duplican sincronización;
- mantienen correctamente el lifetime del slot;
- el commit/release sigue siendo único.

## Fase 4 — Metadata/type support ROS

Crear la abstracción que encapsule:

```text
ROS type name
type support
type validation
serialization
deserialization
```

Resolver aquí el puente entre el tipo ROS runtime y `T`.

## Fase 5 — Publisher genérico

1. Introducir `GenericPublisher`.
2. Registrar el boundary como consumer genérico.
3. Serializar desde el producer tipado antes del commit.
4. Publicar desde el serialized payload del slot.
5. Verificar que el publisher nunca necesita acceder a `T`.

## Fase 6 — Subscriber genérico

1. Introducir `GenericSubscription`.
2. Registrar el boundary como producer genérico.
3. Obtener un producer grant.
4. Colocar/conservar el serialized message en el slot.
5. Deserializar sobre el `void *` correspondiente al `T` preexistente.
6. Realizar un único commit.
7. Verificar que un `ConsumerGrant<T>` convencional recibe correctamente la muestra.

## Fase 7 — Integración completa

Comprobar ambos caminos:

```text
ROS → generic subscriber → Blackboard<T> → algorithm<T>
```

y:

```text
algorithm<T> → Blackboard<T> → generic publisher → ROS
```

Finalmente probar un flujo completo:

```text
ROS input
   │
GenericSubscriber
   │
Blackboard<T1>
   │
Algorithm
   │
Blackboard<T2>
   │
GenericPublisher
   │
ROS output
```

---

# 16. Tests necesarios

## `lfbb`

Los tests existentes deben continuar pasando sin modificaciones semánticas.

Añadir únicamente tests necesarios para comprobar los nuevos puntos de extensión.

## BPE Blackboard

Comprobar al menos:

- registro tipado primero;
- registro genérico primero;
- materialización tardía;
- producer tipado + consumer genérico;
- producer genérico + consumer tipado;
- cálculo correcto del margen;
- ausencia de resize tardío;
- eliminación de entrada nunca materializada;
- validación de tipos incompatibles.

## Grants

Verificar:

- lifetime RAII;
- reserva/release correctos;
- commit único;
- native y serialized corresponden siempre a la misma secuencia;
- ningún consumer puede observar una combinación de representaciones pertenecientes a muestras diferentes.

## ROS

Utilizar inicialmente al menos:

- un mensaje sencillo/fijo;
- un mensaje con campos dinámicos (`string`, `vector` o equivalente);
- más de un tipo ROS para comprobar que la genericidad es real.

---

# 17. V1 — Criterio de finalización

La primera versión se considera terminada cuando sea posible configurar el tipo mediante YAML y ejecutar:

```text
Generic ROS input
      ↓
Blackboard<T>
      ↓
typed algorithm
      ↓
Blackboard<U>
      ↓
Generic ROS output
```

sin escribir código de frontera específico para `T` ni `U`.

En V1 se acepta que:

```text
SerializedMessage
```

realice asignaciones dinámicas durante ejecución.

No debe retrasarse V1 intentando resolver todavía el comportamiento real-time de la serialización.

---

# 18. V2 — Ejecución sin asignaciones

Una vez validada funcionalmente V1, estudiar una segunda implementación de almacenamiento serializado.

Objetivo:

```text
configure/start
    │
    └── allocations permitted

runtime
    │
    └── no allocations in BPE/Blackboard
```

Aspectos a estudiar:

- `rcl_serialized_message_t`;
- buffers preasignados;
- capacidad máxima de mensajes bounded;
- política explícita para mensajes unbounded;
- allocator asociado al almacenamiento del slot;
- reutilización del buffer serializado;
- interacción con `GenericSubscription`;
- interacción con executor;
- posible descenso desde `rclcpp` a `rcl`;
- asignaciones internas de RMW/DDS.

Esta fase no debe modificar el modelo conceptual establecido en V1:

```text
slot = native + serialized
```

ni:

```text
single claim
single commit
single release
```

Sólo debe sustituir la estrategia de almacenamiento y transporte de la representación serializada.

---

# 19. Invariantes de diseño

Durante la implementación deben conservarse explícitamente las siguientes invariantes:

1. **Una muestra corresponde a un slot.**
2. **Native y serialized son dos representaciones de la misma muestra.**
3. **Existe un único estado de sincronización por slot.**
4. **Existe un único commit por muestra.**
5. **Los objetos nativos se crean durante configuración, nunca por mensaje.**
6. **Sólo código que conoce `T` construye los objetos `T`.**
7. **El publisher ROS es un consumer y nunca serializa.**
8. **El subscriber ROS es un producer y deserializa sobre un `T` ya existente.**
9. **Los algoritmos internos continúan utilizando la API tipada.**
10. **ROS permanece restringido a la frontera BPE.**
11. **`lfbb_blackboard` no adquiere dependencias de ROS.**
12. **No se necesita crecimiento del ring después de la materialización.**
13. **Las entradas que nunca adquieren participante tipado se eliminan al finalizar la configuración.**
14. **V1 prioriza validar la arquitectura; V2 aborda la ausencia de asignaciones.**

# 20. Punto técnico a resolver durante la implementación

No queda ninguna decisión arquitectónica bloqueante.

Sí queda una cuestión técnica que debe resolverse antes de implementar la validación de tipos:

> Determinar la forma concreta de asociar y comparar el tipo ROS conocido en runtime por el boundary con el `T` conocido estáticamente por el primer participante tipado.

La solución debe permitir detectar una configuración incoherente antes de materializar los slots.

Esta decisión debe permanecer encapsulada en BPE y no debe modificar el sistema de tipos público de `lfbb_blackboard`.

Conviene resolverla experimentalmente al comenzar la fase de metadata/type support ROS, antes de implementar publisher y subscriber completos.


