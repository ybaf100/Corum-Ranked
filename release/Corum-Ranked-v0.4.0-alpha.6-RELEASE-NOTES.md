Corum Ranked (v0.4.0-alpha.6)

## **밑 Assets 항목을 펼치면 모드를 다운로드 할 수 있습니다 .geode로 끝나는 파일을 다운하세요**
## **잘 모르겠다면 [가이드](https://app.notion.com/p/Corum-Integration-3b9f6f1f3d05808a9237efd47f49e9ce?source=copy_link)를 참고하여 다운하세요.**

# v0.4.0-alpha.6
- Ranked 서버 기본 주소를 `https://corum-ranked.onrender.com`으로 내장했습니다.
- Geode 설정의 서버 URL이 비어 있어도 기본 서버로 자동 연결되도록 수정했습니다.
- Debug Bot 테스트의 구형 `DebugBotMatchService` / `debugBotMatch` 이름을 최신 `DebugBotService` / `debugBot` 구조로 정리했습니다.
- 기존 작업 트리에 남아 CI를 깨뜨리던 구형 Debug Bot 테스트 경로를 alpha.6 호환 smoke test로 덮어쓰도록 했습니다.
- Render production build에서 TypeScript type package가 빠지지 않도록 배포 문서를 수정했습니다.
