# BlockPluginEngine

Se pretende diseñar, antes de implementar, una arquitectura C++ para ROS 2 basada en **un único `rclcpp_lifecycle::LifecycleNode` y un sistema de plugins dinámicos**. El trabajo debe hacerse incrementalmente: primero arquitectura, contratos e interfaces; después implementación.

## Objetivo general

Todo el sistema debe ejecutarse dentro de **un único Lifecycle Node ROS 2**. La funcionalidad estará dividida en Blocks implementados como plugins cargados dinámicamente.

La comunicación interna entre Blocks se realizará principalmente mediante una **Blackboard global, thread-safe**, ya implementada.

ROS 2 debe quedar restringido principalmente a la frontera del sistema: entrada/salida y comunicación con otros componentes externos. Los Blocks internos deberían poder mantenerse independientes de ROS 2 y probarse sin levantar ROS.

---

## Terminología

La terminología adoptada es:

* **BlockPluginEngine**: arquitectura/sistema completo.
* **Block**: unidad funcional conceptual.
* **BlockPlugin**: interfaz C++ mediante la cual se implementa un Block como plugin dinámico.
* **BlockPluginManager**: responsable de carga, instanciación, configuración y lifecycle de los BlockPlugins.
* **BlockScheduler**: responsable de la planificación y ejecución temporal de los Blocks. Será un interfaz (para permitir alternativas a futuro).
* **BPE_SimpleScheduler**: Una implementación sencilla del BlockScheduler. Estará metido en una librería dinámica (o incluso un plugin) dentro del propio paquete y se cargará en el ejecutable. Es el BlockScheduler por defecto.
* **BPE_LifecycleNode**: único `LifecycleNode` ROS 2 del sistema.
* **Blackboard**: infraestructura compartida para intercambio de datos entre Blocks.
* **RosBoundaryBlock**: Block situado en la frontera ROS 2 del sistema y que necesita acceso a determinadas interfaces del nodo ROS 2.

La distinción conceptual importante es:

> **Block describe qué es una unidad funcional; Plugin describe el mecanismo mediante el cual esa unidad se incorpora dinámicamente al sistema.**

Debe distinguirse también entre:

* **tipo de BlockPlugin**
* **instancia de Block**

Una misma clase de plugin puede generar varias instancias con nombres y configuraciones diferentes.

---

# 1. BPE_LifecycleNode

Será el único nodo ROS 2 del proceso y actuará además como **composition root**.

Responsabilidades:

* Implementar el lifecycle estándar ROS 2.
* Crear y conectar los componentes principales del sistema.
* Delegar al `BlockPluginManager` las operaciones de lifecycle.
* Ejecutarse mediante un `rclcpp::Executor` normal y su correspondiente `spin()`.

No debe contener lógica funcional.

El Scheduler es independiente del ROS Executor. Por tanto existen dos mecanismos de despacho distintos:

```text
ROS Executor / spin()
        │
        └── callbacks ROS

BlockScheduler
        │
        └── ejecución planificada de Blocks
```

No deben confundirse ni mezclarse conceptualmente.

---

# 2. Blackboard

La Blackboard ya está implementada y es thread-safe.

No forma parte del esquema de inyección de dependencias del `BlockPluginManager`.

Se obtiene directamente mediante una función global:

```cpp
inline Blackboard& the_blackboard()
{
    static Blackboard instance;
    return instance;
}
```

Por tanto:

* `BlockPluginManager` no conoce la Blackboard.
* `BlockScheduler` no conoce la Blackboard.
* Los Blocks acceden directamente a ella cuando lo necesitan.

La Blackboard constituye el principal mecanismo de comunicación entre Blocks, incluidos los Blocks de frontera.

Conceptualmente:

```text
ROS / TCP / CAN / ...
       │
       ▼
 Boundary Blocks
       │
       ▼
   Blackboard
       ▲
       │
 Internal Blocks
```

---

# 3. BlockPluginManager

Sus responsabilidades serán:

* Cargar la configuración global.
* Determinar qué instancias de Block deben crearse.
* Cargar dinámicamente los tipos de plugin, previsiblemente mediante `pluginlib`.
* Permitir varias instancias del mismo tipo de plugin.
* Gestionar nombre, tipo y configuración de cada instancia.
* Extraer y distribuir a cada instancia su fragmento de configuración.
* Inyectar dependencias externas cuando sean necesarias.
* Mantener la propiedad y lifetime de las instancias.
* Propagar las operaciones de lifecycle.
* Detectar las capacidades adicionales de cada Block mediante las interfaces que implementa.
* Registrar cada Block en los mecanismos correspondientes, por ejemplo el `BlockScheduler`.

El identificador operativo será el **nombre de instancia**, no el tipo de plugin.

Ejemplo:

```yaml
blocks:

  - name: front_encoder
    type: WheelEncoderPlugin

    execution:
      policy: timer
      period: 10ms

    config:
      ...

  - name: rear_encoder
    type: WheelEncoderPlugin

    execution:
      policy: timer
      period: 20ms

    config:
      ...
```

Se prefiere hablar de `blocks:` en la configuración antes que de `plugins:`, porque el usuario está declarando instancias funcionales; el carácter dinámico de plugin es un detalle de implementación arquitectónica.

---

# 4. BlockPlugin

`BlockPlugin` será la interfaz base obligatoria de todos los Blocks.

Su responsabilidad principal será definir:

* configuración
* lifecycle interno simplificado

No se pretende reproducir completamente el lifecycle ROS 2 dentro de cada plugin.

Conceptualmente:

```cpp
class BlockPlugin
{
public:
    virtual ~BlockPlugin() = default;

    virtual Result configure(const BlockConfig& config) = 0;

    virtual Result start() = 0;
    virtual Result stop() = 0;
    virtual Result cleanup() = 0;
};
```

La traducción aproximada sería:

```text
ROS lifecycle                 Block lifecycle

on_configure()     ────────►  configure()
on_activate()      ────────►  start()
on_deactivate()    ────────►  stop()
on_cleanup()       ────────►  cleanup()
```

`BlockPlugin` no debe conocer:

* `rclcpp_lifecycle::State`
* ROS lifecycle callbacks
* política de scheduling
* periodo de ejecución
* detalles del Scheduler

La configuración del Block sigue realizándose siempre a través de `BlockPlugin::configure()`, aunque la capacidad de ejecución se exprese mediante otras interfaces.

---

# 5. Interfaces de capacidad mediante herencia múltiple

La ejecución no formará parte de `BlockPlugin`.

Se utilizará una **jerarquía paralela de interfaces de capacidad**, combinadas mediante herencia múltiple.

Ejemplo:

```cpp
class TimerBlock
{
public:
    virtual ~TimerBlock() = default;
    virtual ExecutionResult execute() = 0;
};
```

Un Block interno podría ser:

```cpp
class MyAlgorithmBlock :
    public BlockPlugin,
    public TimerBlock
{
    ...
};
```

El `BlockPluginManager` trabaja fundamentalmente con `BlockPlugin`.

Las capacidades adicionales se detectarán mediante `dynamic_cast`.

Por ejemplo:

```cpp
if (auto* timer = dynamic_cast<TimerBlock*>(plugin))
{
    scheduler.add(...);
}
```

Esta aproximación debe interpretarse como un sistema de **capability interfaces**.

`BlockPlugin` responde a:

> ¿cómo se configura y gestiona el ciclo de vida del Block?

Las interfaces adicionales responden a preguntas como:

> ¿qué mecanismo de ejecución soporta?

o:

> ¿con qué frontera tecnológica se integra?

La combinación de interfaces define el comportamiento arquitectónico del Block.

---

# 6. BlockScheduler

El Scheduler debe mantenerse claramente separado del Plugin Manager.

Responsabilidad:

> decidir cuándo ejecutar una instancia de Block.

Inicialmente se quiere un modelo muy sencillo.

La primera capacidad será equivalente a un timer:

```cpp
class TimerBlock
{
public:
    virtual ~TimerBlock() = default;
    virtual ExecutionResult execute() = 0;
};
```

El periodo NO pertenece a `TimerBlock`.

La capacidad declara que el Block puede ser ejecutado de forma temporizada, pero la política concreta pertenece a la **configuración de la instancia**.

Por ejemplo:

```yaml
execution:
  policy: timer
  period: 10ms
```

Por tanto:

> **la clase declara qué capacidades soporta; la instancia declara cómo se utilizan.**

El Scheduler no debe poseer los plugins.

La propiedad pertenece al `BlockPluginManager`.

Conceptualmente:

```text
BlockPluginManager
    │
    ├── owns Block instance A
    ├── owns Block instance B
    └── owns Block instance C

BlockScheduler
    │
    └── mantiene referencias/handles a los Blocks ejecutables
```

No se decidirán inicialmente detalles como:

* thread por Block
* thread pool
* prioridades
* deadlines
* CPU affinity
* overrun handling

La arquitectura debe permitir añadirlos posteriormente sin modificar los Blocks funcionales. Esto significa que alternativas de BlockScheduler vendrán dadas como librería dinámicas (o quizás plugins).

---

# 7. BoundaryBlocks

El concepto de BoundaryBlock expresa **pertenencia a una frontera tecnológica**, no un mecanismo de ejecución.

Ejemplos:

```text
RosBoundaryBlock
TcpBoundaryBlock
CanBoundaryBlock
SerialBoundaryBlock
...
```

Un BoundaryBlock representa una conexión entre el núcleo basado en Blackboard y un sistema externo.

La idea general es:

```text
Sistema externo
      │
      ▼
BoundaryBlock
      │
      ▼
Blackboard
```

o, en sentido de salida:

```text
Blackboard
      │
      ▼
BoundaryBlock
      │
      ▼
Sistema externo
```

La interfaz Boundary expresa principalmente:

* qué tecnología externa utiliza el Block;
* qué dependencias específicas deben inyectarse;
* a qué frontera arquitectónica pertenece.

No debe expresar por sí misma cómo se ejecuta el Block.

---

# 8. RosBoundaryBlock

`RosBoundaryBlock` identifica un Block que utiliza ROS 2 como frontera de comunicación.

Su función principal será permitir que el `BlockPluginManager` detecte que ese plugin necesita acceso a determinadas interfaces del nodo ROS 2.

No se inyectará directamente:

```cpp
rclcpp_lifecycle::LifecycleNode&
```

porque daría acceso excesivo a ROS 2.

La idea es inyectar directamente alguna interfaz oficial de `rclcpp`, probablemente:

```cpp
rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr
```

y, si fuera necesario, alguna interfaz base relacionada.

La motivación es limitar el acceso principalmente a:

* publishers
* subscriptions

y evitar exponer innecesariamente:

* services
* actions
* timers ROS
* parámetros
* otras capacidades del nodo

No se intentará encapsular la API templada de publishers/subscriptions detrás de una interfaz virtual propia. Los templates permanecerán en los Blocks concretos.

Conceptualmente:

```cpp
class RosBoundaryBlock
{
public:
    virtual ~RosBoundaryBlock() = default;

    virtual void set_ros_interface(
        rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics) = 0;
};
```

La firma concreta todavía está por decidir, pero esta es una buena candidata.

---

# 9. Frontera tecnológica y mecanismo de ejecución son dimensiones ortogonales

Éste es un principio central del diseño.

`RosBoundaryBlock` significa:

> este Block depende de ROS 2 y pertenece a la frontera ROS.

`TimerBlock` significa:

> este Block necesita ser ejecutado periódicamente por el `BlockScheduler`.

Son conceptos independientes.

Por tanto son combinables mediante herencia múltiple.

---

# 10. Entrada ROS

Un Block de entrada ROS será conceptualmente:

```text
BlockPlugin + RosBoundaryBlock
```

sin `TimerBlock`.

Ejemplo:

```cpp
class RosSensorInput :
    public BlockPlugin,
    public RosBoundaryBlock
{
    ...
};
```

Su ejecución está provocada por el mundo exterior:

```text
mensaje ROS
    │
    ▼
ROS Executor / spin()
    │
    ▼
subscription callback
    │
    ▼
RosBoundaryBlock
    │
    ▼
Blackboard
```

Por tanto:

```text
RosBoundaryBlock + !TimerBlock
        ≈ ROS input
```

No necesita participar en el `BlockScheduler`.

---

# 11. Salida ROS

Un Block de salida ROS será conceptualmente:

```text
BlockPlugin + RosBoundaryBlock + TimerBlock
```

Ejemplo:

```cpp
class RosOdometryOutput :
    public BlockPlugin,
    public RosBoundaryBlock,
    public TimerBlock
{
    ...
};
```

La publicación ROS no necesita `spin()` para ejecutar `publish()`.

Por tanto la salida puede utilizar directamente el mecanismo normal de ejecución del Engine:

```text
BlockScheduler
      │
      ▼
TimerBlock::execute()
      │
      ▼
lee Blackboard
      │
      ▼
publisher.publish(...)
      │
      ▼
ROS
```

Conceptualmente:

```text
RosBoundaryBlock + TimerBlock
        ≈ ROS output
```

Esto evita introducir:

* un `tick()` especial para RosBoundaryBlocks;
* un segundo scheduler específico de ROS;
* timers ROS utilizados únicamente para provocar publicaciones.

ROS permanece como mecanismo de transporte de frontera, mientras que el Engine mantiene el control sobre cuándo se produce la salida.

---

# 12. Regla general input/output

El patrón puede generalizarse a otras tecnologías.

Conceptualmente:

```text
BoundaryBlock + !TimerBlock
        ≈ entrada dirigida por eventos externos

BoundaryBlock + TimerBlock
        ≈ salida activa planificada por el Engine
```

Ejemplos posibles:

```text
RosBoundaryBlock + !TimerBlock
    → input ROS

RosBoundaryBlock + TimerBlock
    → output ROS

TcpBoundaryBlock + !TimerBlock
    → input TCP dirigido externamente

TcpBoundaryBlock + TimerBlock
    → output TCP planificado

CanBoundaryBlock + !TimerBlock
    → recepción CAN

CanBoundaryBlock + TimerBlock
    → transmisión CAN planificada
```

Esta equivalencia es una regla arquitectónica útil, aunque no tiene que considerarse universal para cualquier futura tecnología. Si una frontera concreta tiene necesidades diferentes, podrán añadirse otras interfaces de ejecución.

---

# 13. Separación entre fuente de actividad y tecnología de frontera

El sistema debe distinguir dos preguntas diferentes:

```text
¿Qué tecnología externa utiliza el Block?
```

Esto lo expresan interfaces como:

```text
RosBoundaryBlock
TcpBoundaryBlock
CanBoundaryBlock
```

Frente a:

```text
¿Cómo obtiene actividad el Block?
```

Esto lo expresan interfaces como:

```text
TimerBlock
```

La combinación determina el papel concreto.

Por ejemplo:

```text
BlockPlugin
    │
    ├── TimerBlock
    │      → algoritmo interno periódico
    │
    ├── RosBoundaryBlock
    │      → entrada ROS
    │
    └── RosBoundaryBlock + TimerBlock
           → salida ROS periódica
```

---

# 14. Modelo de ejecución resultante

Existen dos fuentes principales de actividad.

## Actividad externa

Provocada por eventos del exterior:

```text
ROS message
    │
    ▼
ROS Executor
    │
    ▼
subscription callback
    │
    ▼
RosBoundaryBlock
```

## Actividad interna

Provocada por el Engine:

```text
BlockScheduler
    │
    ▼
TimerBlock::execute()
```

Esto permite expresar una regla sencilla:

> **Las entradas son normalmente dirigidas por eventos externos; las salidas son normalmente actividades iniciadas por el Engine.**

Para ROS:

```text
              ROS
               │
        ┌──────┴──────┐
        │             ▲
        ▼             │
     input          output
        │             ▲
 ROS Executor     BlockScheduler
        │             │
        ▼             │
 RosBoundary      RosBoundary
        │             ▲
        └──► Blackboard
```

---

# 15. Configuración de instancia

La configuración debería separar claramente:

```text
identidad
tipo
configuración funcional
configuración de ejecución
```

Conceptualmente:

```cpp
struct BlockInstanceConfig
{
    std::string name;
    std::string type;

    BlockConfig config;
    ExecutionConfig execution;
};
```

Ejemplo de Block interno:

```yaml
blocks:

  - name: localization
    type: LocalizationBlock

    execution:
      policy: timer
      period: 20ms

    config:
      ...
```

Ejemplo de entrada ROS:

```yaml
  - name: odometry_input
    type: RosOdometryInputBlock

    config:
      topic: /odometry
```

Ejemplo de salida ROS:

```yaml
  - name: navigation_output
    type: RosNavigationOutputBlock

    execution:
      policy: timer
      period: 50ms

    config:
      topic: /navigation_result
```

La existencia de `execution` está asociada a las capacidades de ejecución del Block, no a que sea o no BoundaryBlock.

---

# 16. Detección de capacidades por el BlockPluginManager

El Manager podrá detectar independientemente las capacidades.

Conceptualmente:

```cpp
if (auto* ros = dynamic_cast<RosBoundaryBlock*>(plugin.get()))
{
    ros->set_ros_interface(...);
}
```

y:

```cpp
if (auto* timer = dynamic_cast<TimerBlock*>(plugin.get()))
{
    scheduler.add(timer, execution_config);
}
```

Por tanto el Manager no necesita clasificar manualmente un Block como:

```text
input
output
internal
```

Su papel emerge de las interfaces implementadas.

Ejemplo:

```text
BlockPlugin
    → lifecycle/configuración

BlockPlugin + TimerBlock
    → Block interno periódico

BlockPlugin + RosBoundaryBlock
    → entrada ROS

BlockPlugin + RosBoundaryBlock + TimerBlock
    → salida ROS
```

---

# 17. Principios arquitectónicos consolidados

La arquitectura debe preservar las siguientes reglas:

1. Existe un único `LifecycleNode` ROS 2.

2. Los Blocks se cargan dinámicamente mediante plugins.

3. Puede haber múltiples instancias independientes del mismo tipo.

4. La configuración es por instancia.

5. `BlockPlugin` define configuración y lifecycle interno.

6. Las capacidades adicionales se expresan mediante interfaces paralelas y herencia múltiple.

7. El `BlockPluginManager` gestiona existencia, configuración, lifecycle, dependencias y ownership.

8. El `BlockScheduler` gestiona exclusivamente la ejecución temporal.

9. El Scheduler no posee los Blocks.

10. La política de ejecución pertenece a la instancia, no al tipo de Block.

11. La Blackboard es global, thread-safe y accesible mediante `the_blackboard()`.

12. Blackboard no forma parte de la inyección de dependencias.

13. ROS 2 se considera una tecnología de frontera, no parte del diseño interno.

14. `RosBoundaryBlock` expresa dependencia y pertenencia a la frontera ROS, no un modelo de ejecución.

15. Las interfaces de ejecución como `TimerBlock` son ortogonales a las interfaces Boundary.

16. `RosBoundaryBlock + !TimerBlock` representa normalmente una entrada ROS dirigida por callbacks.

17. `RosBoundaryBlock + TimerBlock` representa normalmente una salida ROS planificada por el Engine.

18. Los publishers ROS no necesitan `spin()` para efectuar una publicación.

19. El ROS Executor gestiona la actividad provocada por callbacks externos.

20. El `BlockScheduler` gestiona la actividad iniciada por el propio Engine.

21. No se utilizarán inicialmente services, actions o timers ROS como mecanismos funcionales de los RosBoundaryBlocks.

22. No se debe sobrediseñar inicialmente el Scheduler.

23. El mismo esquema de combinación entre frontera tecnológica y mecanismo de ejecución debe poder extenderse a TCP, CAN u otras tecnologías.

---

# 18. Estructura conceptual actual

```text
                    ┌────────────────────────┐
                    │  BPE_LifecycleNode     │
                    │                        │
                    │ ROS lifecycle          │
                    │ composition root       │
                    └───────────┬────────────┘
                                │
                                ▼
                    ┌────────────────────────┐
                    │ BlockPluginManager     │
                    │                        │
                    │ configuration          │
                    │ pluginlib              │
                    │ instances              │
                    │ lifecycle              │
                    │ dependency injection   │
                    └───────────┬────────────┘
                                │
                           BlockPlugin
                                │
              ┌─────────────────┼─────────────────┐
              │                 │                 │
              ▼                 ▼                 ▼
         TimerBlock      RosBoundaryBlock   otras capacidades
              │                 │
              │                 │
              │          ┌──────┴──────┐
              │          │             │
              │       input          output
              │          │             │
              │     ROS Executor       │
              │                        │
              └───────────────┬────────┘
                              │
                       BlockScheduler
                    cuando existe TimerBlock
```

Una representación funcional más clara es:

```text
ROS input
    │
    ▼
ROS Executor
    │
    ▼
RosBoundaryBlock
    │
    ▼
Blackboard
    ▲
    │
Internal TimerBlocks
    ▲
    │
BlockScheduler
    │
    └──────────────► RosBoundaryBlock + TimerBlock
                             │
                             ▼
                          ROS output
```

La arquitectura busca preservar una separación clara entre:

```text
Lifecycle Node   → estado global ROS 2

Plugin Manager   → existencia, configuración,
                   dependencias y lifecycle

Scheduler        → cuándo ejecutar

Blackboard       → intercambio de información

BoundaryBlock    → conexión con una tecnología externa

TimerBlock       → capacidad de ejecución periódica
```

La idea central resultante es:

> **Las interfaces Boundary describen dónde se conecta un Block.
> Las interfaces de ejecución describen cómo obtiene actividad.
> La combinación de ambas capacidades determina su papel funcional.**

El siguiente paso debería ser diseñar las interfaces C++ concretas de:

* `BlockPlugin`
* `TimerBlock`
* `RosBoundaryBlock`
* `BlockScheduler`
* estructuras de configuración
* relación exacta entre `BlockPluginManager` y `BlockScheduler`

antes de comenzar la implementación.
