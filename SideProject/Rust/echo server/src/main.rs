use tokio::net::TcpListener;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

// 1. C++의 CreateIoCompletionPort와 워커 스레드 풀 초기화를 대신해주는 매크로
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // 2. 비동기 리스너 생성 (bind도 비동기로 동작합니다)
    let listener = TcpListener::bind("127.0.0.1:8081").await?;
    println!("🚀 진정한 비동기(IOCP) Rust 에코 서버가 포트 8081에서 시작되었습니다!");

    // 클라이언트 접속 대기 무한 루프
    loop {
        // 3. accept() 대기. (이 줄에서 스레드가 멈추는 게 아니라 다른 일을 하러 떠납니다)
        let (mut socket, addr) = listener.accept().await?;
        println!("Client connected: {}", addr);

        // 4. 새로운 워커(Task)를 만들어 IOCP 스레드 풀에 던집니다.
        tokio::spawn(async move {
            let mut buf = [0; 1024];

            // 이 내부 루프가 바로 하나의 클라이언트를 전담하는 '비동기 콜백' 영역입니다.
            loop {
                // 5. WSARecv() 호출. (마찬가지로 여기서 스레드가 멈추지 않고 양보합니다)
                let n = match socket.read(&mut buf).await {
                    // socket closed
                    Ok(0) => {
                        println!("Client disconnected (FIN): {}", addr);
                        return;
                    }
                    Ok(n) => n,
                    Err(e) => {
                        eprintln!("Error reading from client {}: {}", addr, e);
                        return;
                    }
                };

                println!("Received from {}: {}", addr, String::from_utf8_lossy(&buf[..n]));

                // 6. WSASend() 호출.
                if let Err(e) = socket.write_all(&buf[..n]).await {
                    eprintln!("Error writing to client {}: {}", addr, e);
                    return;
                }
            }
        });
    }
}