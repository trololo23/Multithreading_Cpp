# Executors and other Structures

```Executors``` - небольшая библиотека помогающая облегчить создание многопоточной программы. Обычно в многопоточных вычислениях можно наткнуться на следющие проблемы:
* Пользователь не может контролировать сколько потоков будет запущено.
Код действует эгоистично и занимает все ядра на машине.
* Неудобно использовать такой код внутри другого параллельного алгоритма.
Например если на первом уровне разобъем задачу на 10 частей и каждую захотим решить с помощью Compute(), то у нас запустится 10 * ```hardware concurrency``` потоков.
* Нельзя отменить вычисление, нельзя следить за прогрессом.

# Пример использования
```
  class MyPrimeSplittingTask : public Task {
      Params params_;
  public:
      MyPrimeSplittingTask(Params params) : params_(params) {}
      bool is_prime = false;
      virtual void Run() {
          is_prime = check_is_prime(params_);
      }
  }

  bool DoComputation(std::shared_ptr<Executor> pool, Params params) {
      auto my_task = std::make_shared<MyPrimeSplittingTask>(params);
      pool->Submit(my_task);
      my_task.Wait();
      return my_task->is_prime;
  }
```

Использование Executors возлагает на полльзователя только разделение задачи на более мелкие подзадачи, немного подробнее:

* ```Task``` - это какой-то кусок вычислений. Сам код вычисления находится в
методе run() и определяется пользователем.
* ```Executor``` - это набор потоков, которые могут выполнять ```Task```-и.
* ```Executor``` запускает потоки в конструкторе, во время работы новых потоков не создаётся.
* Чтобы начать выполнять ```Task```, пользователь должен отправить его в Executor с помощью метода
Submit().
* После этого, пользователь может дождаться пока ```Task``` завершится, позвав метод ```Task::Wait```.

Также есть более функциональные фичи:
* Future - это Task, у которого есть результат (какое-то значение).
* Invoke(cb) - выполнить cb внутри Executor-а, результат вернуть через Future.
* Then(input, cb) - выполнить cb, после того как закончится input. Возвращает Future на результат cb не дожидаясь выполнения input.
* WhenAll(vector<FuturePtr<T>> ) -> FuturePtr<vector<T>> - собирает результат нескольких Future в один.
* WhenAllBeforeDeadline(vector<FuturePtr<T>>, deadline) -> FuturePtr<vector<T>> - возвращает все результаты, которые успели появиться до deadline.
  
