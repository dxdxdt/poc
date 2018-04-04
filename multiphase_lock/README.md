# Multiphase Lock
## 서론
분산 시스템 내의 lock 처리는 한 머신 내에서 OS API가 제공하는 mutex나 semaphore, atomic 같은 기능의 도움을 받을 수 없다. 현재까지 정형화된 방법이 없으며, 소프트웨어를 개발할 때 직접 만들어야 하는 기능 중 하나이다.
분산 시스템에서의 데이터 업데이트로써 전통적인 방법으로 Two-phase commit(2PC)가 있다고 한다. 이 방법은 한 시스템이 commit 할 데이터를 만든 후 다른 동등한 시스템들에 그 데이터가 commit 되어도 되는지 확인을 받는 방식이다. 여기서 확인을 받는 단계가 1단계, 모두가 확인한 후 실제로 데이터를 commit 하여 영구적으로 만드는 단계를 2단계라고 한다. 하지만 이 방법은 한계가 있는데, commit을 하려는 데이터가 이미 "결정되어" 있어야 한다는 것이다. 이미 결정된 데이터의 예로 금융 전산망의 카드 승인이나 은행 송금, 또는 이메일 전송 등이 있다. 이 예들은 모두 서비스 사용자가 영구적으로 되어야 하는 데이터를 만들어 놓은 예이다(결제 금액, 메일의 내용). 만약 결정되어 있지 않은 데이터를 분산 시스템 중 하나가 생성해야 하면 어떨까?

이 문제를 FIX Keep(분산 매치메이킹 게임 서비스)을 설계하면서 발견하게 되었다. FIX Keep은 플레이어가 어느 방에서 플레이하게 해야 할지 결정하는 master 서버와 플레이어가 실제로 접속해 게임을 플레이하는 방(게임 프로세스)을 실행하는 worker들이 각각 다수의 시스템상에서 서비스한다. 이렇게 하는 이유는 부하 분산뿐만 아니라 가용성 역시 극대화하기 위해서이다. 이상적인 구조는 한 AWS(Amazon Web Service) 리전에 각 가용 영역 한 곳당 한 인스턴스 위에서 작동하는 master 서버와 worker 서버 여러 인스턴스로 구성하는 것이다. 서울 리전에 전개를 한다고 가정했을 때, A와 C 가용 영역이 있으므로 최소 master 서버 2 인스턴스와 worker 서버 6 인스턴스(영역당 3개씩)를 운영하는 것이다. 이렇게 해서 한 가용 영역의 catastrophic failure를 예방하는 것이다.
게임 클라이언트는 접속할 방 정보를 받기 위해 master 서버에 접속하여 매칭 요청을 한다. Master 서버는 이 요청이 들어오면 각 게임 프로세스들 중 플레이어가 플레이하기 적절한 곳을 결정하여[1] 게임 클라이언트에게 통보해 주어야 한다. 이를 위해 master 서버는 내부 채널을 통해 해당 플레이어가 그 게임 프로세스에 접속할 것이라는 통보와 확인하는 절차를 거친다. "적절한 곳"을 결정하는 과정은 여유가 있는 방이 있다면 큰 문제가 아니다. Master 서버가 후보 방 중에서 가장 여유가 있는 곳으로 매칭을 시도해 보고, 그사이에 다른 master 서버가 그 여유분을 없앴다면 다른 후보를 모색하는 방법으로 계속 방을 찾아 나가면 된다. 즉, 게임 프로세스가 lock 지점이 되는 것이다. 그러다가 이미 만들어진 모든 방에 여유가 없으면 worker에게 새로운 게임 프로세스를 생성하는 명령을 내려야 한다. 여기서 문제가 시작된다. 게임 프로세스는 비싸다.
이 상황에서 master 서버가 바로 게임 프로세스 생성 명령을 worker에게 내려 방을 생성한 뒤 게임 클라이언트에게 그곳에 접속하도록 응답을 줄 수 있다. 이 방법은 초당 매칭 요청 수가 적으면 표면적으로 나타나지 않겠지만, 많아질 경우 경쟁 상태로 인해 같은 플레이어 군을 위한 여러 게임 프로세스가 만들어지는 경우가 발생할 것이다. 동일 플레이어 군을 위한 방이 다수가 되어 해당 방들의 플레이어 유입이 느려지게 된다(각 방의 플레이어 증가 속도가 느려짐). 이는 시스템 자원 면에서 비효율적이며, 사용자 경험에도 좋지 않다.

그래서 고안해낸 것이 multiphase lock이다. Multiphase lock은 분산된 시스템 내의 한 자원의 경쟁 상태를 **완화**하는 기법이다. 방식은 한 피어가 다른 시스템 내의 다른 피어에 동의를 얻는 데에서 2PC와 유사하다. 여기서 말하는 **피어**는 주로 컴퓨팅의 최소 처리 단위인 스레드를 의미한다.
물론 이 문서에서 설명하려는 multiphase lock이 기존 개발된 알고리즘에서 많은 변화가 있는 것은 아니다. 잘 알려진 알고리즘인 *[Lamport's distributed mutual exclusion algorithm](https://en.wikipedia.org/wiki/Lamport%27s_distributed_mutual_exclusion_algorithm)*과 비교해 볼 때, 이 알고리즘에서 사용되는 "logical clock"인 *Lamport timestamps*를 사용하는 대신, 미리 제공된 우선순위 값을 사용한다는 차이점이 있다. 또한 *[Ricart–Agrawala algorithm
From](https://en.wikipedia.org/wiki/Ricart%E2%80%93Agrawala_algorithm)* 알고리즘과 비교해볼 때, 이 알고리즘에서는 "lock을 해제한다는 메시지"를 보내는 단계를 없애는 최적화를 시도했지만, multiphase lock은 이 메시지를 사용한다. 이를 사용할 경우의 문제는 우선순위가 높은 한 피어가 계속 lock 요청을 할 경우 다른 피어가 starvation 상태가 되기 때문이다. Logical clock을 사용하지 않고 고정된 우선순위 값을 사용하는 이유도 starvation 상태를 피하기 위해서이다.

## 본론
Lock은 다음 상태 중 하나의 상태로 존재한다:

1. **NONE**: 피어가 lock을 얻으려 하지 않는 최초 상태
1. **LURKING**: 피어가 lock을 얻으려 하는 상태이지만, 다른 모든 피어가 lock을 포기할 때까지 "기다리는" 상태
1. **SOLICITING**: 피어가 lock을 얻으려고 다른 피어에 동의 요청하는 상태
1. **ACQUIRED**: 다른 모든 피어로부터 동의를 얻어 lock을 얻은 상태

Multiphase lock 구현을 위해 피어들이 서로 나누는 메시지의 종류는 다음과 같다:

1. **MyLock**: 다른 모든 피어에 lock 동의를 요청하는 메시지
1. **YourLock**: 한 피어가 다른 피어로부터 MyLock 메시지를 받은 뒤, 그 피어에 lock을 허락하는 메시지
1. **LockReset**: 한 피어가 lock을 포기하기 위해 다른 모든 피어에 보내는 메시지

한 피어가 lock을 얻기 위한 간략한 흐름은 다음과 같다:

1. lock을 얻는 중인 상태로 진입한다. 이때,
  * 피어 중 MyLock 메시지를 발송한 후 LockReset을 발송하지 않은 피어가 있을 시에는 LURKING 상태로 진입한다.
    * MyLock 메시지를 보냈던 모든 피어가 LockReset 메시지를 발송하여 아무도 lock을 획득하려하지 않을 때, 모든 피어에 MyLock 메시지를 발송한 뒤 SOLICITING 상태로 진입한다.
  * 그렇지 않다면 모든 피어에 MyLock 메시지를 발송한 뒤 SOLICITING 상태로 진입한다.
1. SOLICITING 상태일 때, 모든 피어로부터 YourLock 메시지를 수신하면 ACQUIRED 상태로 진입한다.
1. ACQUIRED 상태에서 공유된 자원을 사용한다.
1. 자원 사용을 끝낸 뒤, 모든 피어에 LockReset 메시지를 발송한 뒤 NONE 상태로 진입한다.

이 알고리즘이 작동하기 위해서는 모든 피어가 아래의 규칙에 따라 메시지를 처리하거나 lock 획득하거나 반환해야 한다.

* Lock을 걸려 할 때
  * 시스템 내에 다른 피어가 없다면 ACQUIRED 상태로 바로 진입한다.
  * 다른 피어가 lock을 걸려 하지 않는 상황이라면, MyLock 메시지를 모든 피어에 전달하고 SOLICITING 상태로 진입한다.
  * 그렇지 않으면 LURKING 상태로 진입한다.
* 다른 피어로부터 MyLock 메시지를 수신했을 때
  * NONE, LURKING 상태일 때
    * 그 피어에 YourLock 메시지를 발송한다.
  * SOLICITING 상태일 때
    * 그 피어의 우선순위가 높으면 그 피어에 YourLock 메시지를 전달한다.
    * 그렇지 않다면 그 피어를 "기억한다"[2].
* 다른 피어로부터 YourLock 메시지를 수신했을 때
  * SOLICITING 상태일 때
    * 모든 피어로부터 메시지를 받았는지 확인한다. 받았을 경우 ACQUIRED 상태로 진입한다.
  * SOLICITING 상태가 아닐 때는 이 메시지를 무시한다.
* 다른 피어로부터 LockReset 메시지를 수신했을 때
  * LURKING 상태일 때
    * 모든 피어에 MyLock 메시지를 보낸 뒤 SOLICITING 상태로 진입한다.
* Lock을 풀거나 포기하려 할 때
  * ACQUIRED 혹은 SOLICITING 상태일 때
    * "기억된" 피어들에 YourLock 메시지를 발송한다.
    * 모든 피어들에 LockReset 메시지를 보낸다.
  * SOLICITING 상태일 때
    * 모든 피어들에 LockReset 메시지를 보낸다.
  * NONE 상태로 진입한다.
* 피어와의 연결이 끊겼을 때
  * LURKING 상태일 때
    * 시스템 내에 다른 피어가 없다면 바로 ACQUIRED 상태로 진입한다.
    * 시스템 내에 다른 피어가 lock을 획득하지 않는다면, MyLock 메시지를 모든 피어에 발송하고 SOLICITING 상태로 진입한다.
  * SOLICITING 상태일 때
    * 끊긴 피어가 YourLock 메시지를 발송하기를 기다리는 마지막 피어였다면 ACQUIRED 상태로 진입한다.

피어는 어느 시점에서든지 lock을 획득 도중 포기할 수 있다.

## 참조
- https://www.cs.nmsu.edu/~arao/courses/cs574/mutex/
- https://en.wikipedia.org/wiki/Lamport%27s_distributed_mutual_exclusion_algorithm
- https://en.wikipedia.org/wiki/Ricart%E2%80%93Agrawala_algorithm

## 각주
1. 플레이하기 적절한 곳을 선정해 주는 일반적인 이유는 플레이어가 실력에 맞는 플레이어들과 같이 플레이를 하도록 하기 위해서이다. 뿐만 아니라 다른 이유 때문에 플레이어들을 격리시켜야 하는 경우가 있을 수 있다.
1. ACQUIRED 상태에서 나올 때 그 피어들에게 YourLock을 주기 위해서이다. 이를 *Lamport's distributed mutual exclusion algorithm*는 "deferred"라 표현하는 부분이다.
