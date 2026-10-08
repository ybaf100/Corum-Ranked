Corum Ranked (v0.4.0-alpha.26)

## **밑 Assets 항목을 펼치면 모드를 다운로드 할 수 있습니다 .geode로 끝나는 파일을 다운하세요**
## **잘 모르겠다면 [가이드](https://app.notion.com/p/Corum-Integration-3b9f6f1f3d05808a9237efd47f49e9ce?source=copy_link)를 참고하여 다운하세요.**
## Windows는 .exe 파일로 딸깍설치가 가능합니다!

# v0.4.0-alpha.26
- 100% Clear 직후 오래된 progress 응답이 점수/클리어를 되돌릴 수 있던 문제 수정
- LevelInfo에서 정상 시작한 Attempt가 PlayLayer 생성 중 상태 갱신 때문에 서버 Start를 놓치는 race 보강
- 이전 라운드의 optimistic 점수/클리어가 다음 라운드 HUD에 남는 문제 수정
- FINAL/LAST ATTEMPT 종료 후 ROUND_SETTLING에서 가짜 다음 Attempt가 보이는 문제 수정
- Trigger Death Match 등에서 PREPARE 상태를 놓쳤을 때 Loading Map에 고착되는 문제에 LevelInfo 복구 경로 추가
- LevelInfo 준비창 문구를 ROUND 1 / ROUND 2 - MATCH POINT / ROUND 3 - TIEBREAKER / DEATH MATCH로 변경
