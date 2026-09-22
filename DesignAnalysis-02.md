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
* **BlockScheduler**: responsable de la planificación y ejecución temporal de los Blocks internos.
* **BlockLifecycleNode**: único `LifecycleNode` ROS 2 del sistema.
* **Blackboard**: infraestructura compartida para intercambio de datos entre Blocks.
* **RosBoundaryBlock**: Block situado en la frontera ROS 2 del sistema.

La distinción conceptual importante es:

> **Block describe qué es una unidad funcional; Plugin describe el mecanismo mediante el cual esa unidad se incorpora dinámicamente al sistema.**

Debe distinguirse también entre:

* **tipo de BlockPlugin**
* **instancia de Block**

Una misma clase de plugin puede generar varias instancias con nombres y configuraciones diferentes.

---

# 1. BlockLifecycleNode

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
        └── callbacks ROS de RosBoundaryBlocks

BlockScheduler
        │
        └── ejecución de Blocks internos
```

No deben confundirse ni mezclarse.

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
       ▲
       │
       ▼
 Boundary Blocks
       ▲
       │
       ▼
   Blackboard
       ▲
       │
       ▼
 Internal Blocks
```

---

# 3. BlockPluginManager

Sus responsabilidades serán:

* Cargar la configuración global.
* Determinar qué instancias de Block deben crearse.
* Cargar dinámicamente los tipos de plugin mediante `pluginlib`.
* Permitir varias instancias del mismo tipo de plugin.
* Gestionar nombre, tipo y configuración de cada instancia.
* Extraer y distribuir a cada instancia su fragmento de configuración.
* Inyectar dependencias externas cuando sean necesarias.
* Mantener la propiedad y lifetime de las instancias.
* Propagar las operaciones de lifecycle.
* Detectar las capacidades adicionales de cada Block y registrar la instancia donde corresponda.

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

El `BlockPluginManager` trabaja únicamente con `BlockPlugin`.

El `BlockScheduler` recibirá referencias a los plugins y detectará sus capacidades mediante `dynamic_cast`.

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

Las interfaces adicionales responden a:

> ¿qué capacidades de ejecución o integración tiene este Block?

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

La arquitectura debe permitir añadirlos posteriormente sin modificar los Blocks funcionales.

---

# 7. RosBoundaryBlock

ROS 2 debe quedar limitado a la frontera del sistema.

Los Blocks que interactúan con ROS 2 implementarán una capacidad adicional denominada:

```cpp
RosBoundaryBlock
```

El nombre se adopta deliberadamente porque expresa el papel arquitectónico del Block y deja abierta una familia natural:

```text
RosBoundaryBlock
TcpBoundaryBlock
CanBoundaryBlock
SerialBoundaryBlock
...
```

Un BoundaryBlock representa una conexión entre el núcleo basado en Blackboard y un sistema externo.

Ejemplo conceptual:

```text
ROS topic
   ▲
   │
   ▼
RosBoundaryBlock
   ▲
   │
   ▼
Blackboard
   ▲
   │
   ▼
Internal Blocks
```

---

# 8. Acceso restringido a ROS 2

No se inyectará directamente:

```cpp
rclcpp_lifecycle::LifecycleNode&
```

porque daría acceso excesivo a ROS 2 y establece una dependencia demasiado fuerte.

La idea es inyectar directamente una interfaz oficial de `rclcpp`, probablemente:

```cpp
rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr
```

y, si fuera necesario, también alguna interfaz base relacionada.

La motivación es:

* permitir publishers
* permitir subscriptions
* evitar exponer toda la API del nodo

Los RosBoundaryBlocks NO deberían crear:

* services
* actions
* timers ROS
* otras fuentes de ejecución autónoma

ROS 2 se considera únicamente la frontera de comunicación, no parte del diseño interno del Engine.

La API cómoda para crear publishers y subscriptions seguirá siendo templada dentro del Block concreto, por ejemplo mediante las funciones `rclcpp::create_publisher<T>()` y `rclcpp::create_subscription<T>()`.

No se intentará encapsular esa API templada detrás de una interfaz virtual propia.

---

# 9. Separación entre dominio ROS y dominio Scheduler

Se establece una regla arquitectónica fuerte:

> **Un RosBoundaryBlock no participa en el BlockScheduler.**

Por tanto:

```text
RosBoundaryBlock
       │
       └── ejecutado mediante callbacks del ROS Executor

TimerBlock
       │
       └── ejecutado mediante BlockScheduler
```

Esto evita que un mismo Block pueda ejecutarse concurrentemente desde:

```text
ROS callback
+
Scheduler execute()
```

La comunicación entre ambos dominios se realiza exclusivamente a través de la Blackboard thread-safe.

Conceptualmente:

```text
           ROS Executor
                │
                ▼
        RosBoundaryBlock
                │
                ▼
            Blackboard
                ▲
                │
          Internal Block
                ▲
                │
         BlockScheduler
```

Esta separación simplifica notablemente el modelo de concurrencia.

---

# 10. Compatibilidad de capacidades

Inicialmente habrá al menos:

```text
BlockPlugin        obligatorio

TimerBlock         opcional
RosBoundaryBlock   opcional
```

La combinación:

```text
TimerBlock + RosBoundaryBlock
```

debe considerarse inválida dentro del diseño actual.

El `BlockPluginManager` puede comprobar esta condición mediante `dynamic_cast` y rechazar la configuración.

No se debe generalizar esta exclusión a todos los futuros BoundaryBlocks.

Por ejemplo, un futuro:

```cpp
class TcpInput :
    public BlockPlugin,
    public TcpBoundaryBlock,
    public TimerBlock
```

podría ser perfectamente válido si se decide implementar TCP mediante polling ejecutado por el Scheduler.

Por tanto, la incompatibilidad es específica entre:

```text
RosBoundaryBlock
y
TimerBlock / BlockScheduler
```

y no entre:

```text
BoundaryBlock
y
Scheduler
```

en general.

---

# 11. Configuración de instancia

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

Ejemplo:

```yaml
blocks:

  - name: localization
    type: LocalizationBlock

    execution:
      policy: timer
      period: 20ms

    config:
      ...

  - name: odometry_input
    type: RosOdometryInputBlock

    config:
      topic: /odometry
```

En el segundo caso no habría configuración de Scheduler porque el Block es un `RosBoundaryBlock`.

---

# 12. Principios arquitectónicos consolidados

La arquitectura debe preservar las siguientes reglas:

1. Existe un único `LifecycleNode` ROS 2.

2. Los Blocks se cargan dinámicamente mediante plugins.

3. Puede haber múltiples instancias independientes del mismo tipo.

4. La configuración es por instancia.

5. `BlockPlugin` define configuración y lifecycle interno.

6. Las capacidades adicionales se expresan mediante interfaces paralelas y herencia múltiple.

7. El BlockPluginManager gestiona existencia, configuración, lifecycle y ownership.

8. El BlockScheduler gestiona exclusivamente la ejecución temporal.

9. El Scheduler no posee los Blocks.

10. La política de ejecución pertenece a la instancia, no al tipo de Block.

11. La Blackboard es global, thread-safe y accesible mediante `the_blackboard()`.

12. Blackboard no forma parte de la inyección de dependencias.

13. ROS 2 se considera una tecnología de frontera, no parte del diseño interno.

14. Los RosBoundaryBlocks pueden crear publishers y subscriptions, pero no mecanismos ROS de ejecución autónoma como timers, services o actions.

15. Un RosBoundaryBlock no participa en el BlockScheduler.

16. ROS Executor y BlockScheduler son mecanismos de ejecución independientes.

17. La comunicación entre el dominio ROS y los Blocks internos se realiza mediante la Blackboard.

18. No se debe sobrediseñar inicialmente el Scheduler.

---

# 13. Estructura conceptual actual

```text
                    ┌────────────────────────┐
                    │  BlockLifecycleNode    │
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
             ┌──────────────────┴──────────────────┐
             │                                     │
             ▼                                     ▼
        TimerBlock                         RosBoundaryBlock
             │                                     │
             ▼                                     ▼
      BlockScheduler                         ROS Executor
             │                                     │
             └──────────────┐         ┌────────────┘
                            ▼         ▼
                             Blackboard
```

La arquitectura busca preservar una separación muy clara entre:

```text
Lifecycle Node   → estado global ROS 2
Plugin Manager   → existencia/configuración/lifecycle
Scheduler        → cuándo ejecutar
Blackboard       → intercambio de información
Boundary Blocks  → conexión con sistemas externos
```

El siguiente paso debería ser diseñar las interfaces C++ concretas de:

* `BlockPlugin`
* `TimerBlock`
* `RosBoundaryBlock`
* `BlockScheduler`
* estructuras de configuración
* relación exacta entre `BlockPluginManager` y `BlockScheduler`

antes de comenzar la implementación.
