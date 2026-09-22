# Block Plugin Engine

Se pretende diseñar, antes de implementar, una arquitectura C++ para ROS 2 basada en **un único Lifecycle Node y un sistema de plugins dinámicos**. Quiero trabajar incrementalmente: primero arquitectura e interfaces, y solo después implementación.

### Objetivo general

Todo el sistema debe ejecutarse dentro de **un único `rclcpp_lifecycle::LifecycleNode`**. La funcionalidad estará dividida en plugins cargados dinámicamente. Los plugins se comunicarán principalmente mediante una **Blackboard que ya está implementada y es thread-safe**.

Algunos plugins concretos podrán necesitar ROS 2 directamente para crear publishers, subscriptions, etc. En esos casos se les podrá inyectar como dependencia una referencia a la interfaz apropiada del nodo ROS2 (o a la base de las interfaces de los nodos). La mayoría de plugins deberían poder ser completamente independientes de ROS 2 y probarse sin levantar ROS.

### Componentes principales

**1. Lifecycle Node**

Es el único nodo ROS 2 del proceso y actúa además como *composition root* del sistema.

Debe implementar el lifecycle estándar de ROS 2 (`configure`, `activate`, `deactivate`, `cleanup`, `shutdown`, etc.) y delegar las operaciones correspondientes al resto del sistema. No debe contener lógica funcional.

**2. Plugin Manager**

Sus responsabilidades serán:

* Cargar la configuración global del sistema.
* Determinar qué plugins deben instanciarse.
* Cargar dinámicamente los tipos de plugin, previsiblemente mediante `pluginlib`.
* Permitir crear **varias instancias del mismo tipo de plugin**, cada una con nombre y configuración diferentes.
* Extraer y distribuir a cada instancia su fragmento de configuración.
* Inyectar las dependencias necesarias: Blackboard y, opcionalmente, acceso al nodo ROS 2.
* Mantener la propiedad/colección de las instancias.
* Propagar a los plugins las operaciones asociadas al lifecycle.

Debe distinguirse claramente entre **tipo de plugin** e **instancia de plugin**. El identificador operativo será el nombre de instancia.

Ejemplo conceptual:

```yaml
plugins:
  - name: front_encoder
    type: WheelEncoderPlugin
    execution:
      policy: periodic
      period: 10ms
    config:
      ...

  - name: rear_encoder
    type: WheelEncoderPlugin
    execution:
      policy: periodic
      period: 20ms
    config:
      ...
```

**3. Scheduler**

Es responsable exclusivamente de decidir **cuándo ejecutar cada instancia de plugin**. Debe mantenerse separado del Plugin Manager.

Inicialmente queremos algo deliberadamente sencillo, pero extensible. Las primeras políticas podrían ser:

* `PERIODIC`: ejecución cada cierto periodo.
* `AS_FAST_AS_POSSIBLE`: nueva ejecución tan pronto como sea posible.

La política de ejecución debe pertenecer a la **configuración de la instancia**, no al tipo/clase del plugin. El plugin solo implementa su operación `execute()` y no debería necesitar saber cómo está siendo planificado.

No queremos decidir todavía detalles como thread dedicado, thread pool, prioridades, deadlines, CPU affinity, gestión de overruns, etc. La arquitectura debe permitir añadirlos posteriormente sin modificar los plugins funcionales.

Conviene utilizar el nombre `Scheduler` para no confundir este componente con `rclcpp::Executor`.

**4. Blackboard**

Ya está desarrollada y preparada para acceso concurrente/thread-safe. Es el mecanismo principal de intercambio de información entre plugins.

Los plugins de entrada/salida podrán conectar ROS 2 con la Blackboard, mientras que los plugins internos podrán trabajar exclusivamente contra ella.

### Contrato de Plugin

Queremos una interfaz mínima. Conceptualmente:

```cpp
class BlockPlugin
{
public:
    virtual ~Plugin() = default;

    virtual void configure(/* config + dependencies */) = 0;

    virtual void start() {}
    virtual void stop() {}
    virtual void cleanup() {}

    virtual void execute() = 0;
};
```

Entre las dependencias debe estar la `blackboard` y la interfaz del nodo ROS2.
El block debe ser agnostico al modelo de ejecución.

Esta interfaz es todavía provisional. Hay que estudiar si merece la pena reproducir completamente el lifecycle ROS 2 en cada plugin o mantener una interfaz simplificada (`configure/start/stop/cleanup`).Adicionalmente habrá que considerar
cómo trabajar con los resultados de cada método, especialmente habrá que considerar
si dar un resultado `success` o qué pasa con las excepciones. Lo mismo habrá que pensar respecto de `execute`, naturalmente éste asociado al scheduler.

El acceso a ROS 2 debe ser **opcional**. No queremos crear inicialmente jerarquías diferentes como `Plugin`/`RosPlugin` salvo que aparezca una razón arquitectónica clara para ello.

### Principios que queremos preservar

* Un único nodo ROS 2.
* Plugins cargados dinámicamente.
* Varias instancias independientes del mismo tipo de plugin.
* Configuración por instancia.
* Blackboard como principal mecanismo de comunicación interna.
* ROS 2 como dependencia opcional de algunos plugins, especialmente los de frontera (I/O).
* Los plugins funcionales deberían poder probarse sin ROS 2 siempre que no necesiten explícitamente sus servicios.
* Separación clara entre:

  * **Lifecycle Node:** estado global ROS 2.
  * **Plugin Manager:** existencia, configuración y lifecycle de las instancias.
  * **Scheduler:** ejecución temporal.
  * **Blackboard:** intercambio de datos.
* No sobrediseñar inicialmente el Scheduler; debe ser sencillo pero permitir evolución futura.

Quiero continuar desde aquí revisando primero críticamente esta arquitectura y después diseñar las **interfaces C++, estructuras de configuración y relaciones entre componentes**, antes de empezar a implementar.

### Nombre y terminología del sistema

El nombre provisional adoptado para la arquitectura es **BlockPluginEngine**.

El término **Plugin** es deliberado y forma parte del concepto arquitectónico: el sistema está específicamente diseñado alrededor de componentes cargados dinámicamente como plugins. No se pretende una arquitectura distribuida basada en múltiples nodos ROS 2 ni una composición de componentes enlazados estáticamente.

La terminología inicial será:

* **BlockPluginEngine**: arquitectura/sistema completo.
* **Block**: unidad funcional conceptual.
* **BlockPlugin**: interfaz C++ mediante la cual se implementa un Block como plugin dinámico.
* **BlockPluginManager**: carga, instanciación, configuración y gestión del lifecycle de los BlockPlugins.
* **BlockScheduler**: planificación y ejecución temporal de los BlockPlugins.
* **BlockLifecycleNode**: único Lifecycle Node ROS 2 que contiene el sistema.
* **Blackboard**: infraestructura compartida para el intercambio de datos entre Blocks.

Queremos mantener esta distinción conceptual: **Block describe qué es una unidad funcional; Plugin describe el mecanismo mediante el cual esa unidad se incorpora dinámicamente al sistema**.
