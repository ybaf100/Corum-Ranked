#include "SurvivalReplayPlayback.hpp"
#include "SurvivalReplayStorage.hpp"
#include <map>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <cerrno>
#include <unistd.h>
#endif
namespace corum::ranked::replay {
namespace fs=std::filesystem;
// The SDK save directory is trusted, but its ancestors may be OS aliases
// (/var -> /private/var on Apple). Resolve those before enforcing the replay
// subtree's no-symlink policy. Never canonicalize a child supplied by a file.
Storage::Storage(fs::path root):m_root([&]{std::error_code ec;auto parent=fs::weakly_canonical(root.parent_path(),ec);return ec?root:parent/root.filename();}()),m_worker([this]{
    for(;;){std::function<void()> task;{std::unique_lock lock(m_mutex);m_cv.wait(lock,[this]{return m_stop||!m_queue.empty();});
      if(m_stop&&m_queue.empty())return;task=std::move(m_queue.front());m_queue.pop_front();}task();}
}){}
Storage::~Storage(){{std::lock_guard lock(m_mutex);m_stop=true;}m_cv.notify_one();m_worker.join();}
std::string Storage::error()const{std::lock_guard lock(m_mutex);return m_error;}
void Storage::safeDirectories(fs::path const& dir){
    fs::path at;for(auto const& part:dir){at/=part;if(fs::is_symlink(fs::symlink_status(at)))throw std::runtime_error("Replay symlink refused");}
    fs::create_directories(dir);
}
void Storage::diskBudget(std::uintmax_t additional){
    constexpr std::uintmax_t PendingLimit=128*1024*1024,Reserve=16*1024*1024;
    safeDirectories(m_root/"pending");std::uintmax_t bytes=0;
    for(auto const& f:fs::directory_iterator(m_root/"pending"))if(f.is_regular_file()&&!f.is_symlink()){
        auto size=f.file_size();if(size>PendingLimit-bytes)throw std::runtime_error("Replay pending capacity reached; best preserved");bytes+=size;
    }
    if(additional>PendingLimit-bytes||fs::space(m_root).available<additional+Reserve)throw std::runtime_error("Replay storage full; best preserved");
}
void Storage::durable(fs::path const& path){
#if defined(_WIN32)
    auto h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Replay sync failed");auto ok=FlushFileBuffers(h);CloseHandle(h);if(!ok)throw std::runtime_error("Replay sync failed");
#else
    bool directory=fs::is_directory(path);auto fd=::open(path.c_str(),directory?O_RDONLY:O_RDWR);if(fd<0)throw std::runtime_error("Replay sync failed");auto ok=::fsync(fd);auto error=errno;::close(fd);if(ok!=0&&!(directory&&(error==EINVAL||error==ENOTSUP)))throw std::runtime_error("Replay sync failed");
#endif
}
void Storage::replace(fs::path const& from,fs::path const& to){
    if(fs::is_symlink(fs::symlink_status(to)))throw std::runtime_error("Replay symlink refused");
#if defined(_WIN32)
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Replay atomic replace failed");
#else
    fs::rename(from,to);durable(to.parent_path());
#endif
}
fs::path Storage::pending(std::string const& attempt)const{return m_root/"pending"/(sha256Text(attempt)+".tmp");}
Json Storage::manifest()const{
    auto path=m_root/"manifest.json";if(!fs::exists(path)){Json j;j["formatVersion"]=FormatVersion;return j;}
    if(fs::is_symlink(fs::symlink_status(path))||fs::file_size(path)>4*1024*1024)throw std::runtime_error("Replay manifest refused");
    std::ifstream in(path);std::string text((std::istreambuf_iterator<char>(in)),{});auto j=Json::parse(text);
    if(!j||integer(j.unwrap(),"formatVersion")!=FormatVersion)throw std::runtime_error("Replay manifest damaged; best files preserved");return j.unwrap();
}
void Storage::saveManifest(Json const& j){safeDirectories(m_root);auto next=m_root/"manifest.next";
    if(fs::is_symlink(fs::symlink_status(next)))throw std::runtime_error("Replay symlink refused");
    {std::ofstream out(next,std::ios::binary|std::ios::trunc);out<<j.dump(matjson::NO_INDENTATION);out.flush();if(!out)throw std::runtime_error("Replay manifest write failed");}durable(next);replace(next,m_root/"manifest.json");
}
std::future<void> Storage::begin(Json metadata){return submit([this,metadata]{
    if(!metadataValid(metadata)||manifest()["pending"].size()>=MaxPending)throw std::runtime_error("Replay metadata invalid or pending limit");
    auto id=str(metadata,"attemptId");if(str(manifest()["best"][roundStorageKey(metadata)],"attemptId")==id)throw std::runtime_error("Replay Attempt already preserved");auto file=pending(id);safeDirectories(file.parent_path());
    diskBudget(metadata.dump(matjson::NO_INDENTATION).size()+1);
    if(fs::exists(file))throw std::runtime_error("Replay Attempt already journaled");
    {std::ofstream out(file,std::ios::binary);out<<metadata.dump(matjson::NO_INDENTATION)<<'\n';out.flush();if(!out)throw std::runtime_error("Replay header write failed");}durable(file);
    m_pending[id]={metadata,sha256Text(metadata.dump(matjson::NO_INDENTATION)),0,0,metadata.dump(matjson::NO_INDENTATION).size()+1};
    auto m=manifest();m["pending"][sha256Text(id)]=metadata;saveManifest(m);
});}
std::future<void> Storage::append(std::string attempt,std::vector<Json> records,bool final){
    std::size_t charge=0;for(auto const&r:records)charge+=r.dump(matjson::NO_INDENTATION).size();return submit([this,attempt,records=std::move(records),final]{
    auto found=m_pending.find(attempt);if(found==m_pending.end())throw std::runtime_error("Replay writer missing Attempt");auto& p=found->second;
    if(records.empty()||records.size()>MaxBlockRecords||p.count+records.size()>MaxRecords)throw std::runtime_error("Replay record budget exceeded");
    auto b=block(records,p.chain,p.sequence);auto encoded=b.dump(matjson::NO_INDENTATION)+"\n";
    if(encoded.size()>2*1024*1024)throw std::runtime_error("Replay block budget exceeded; existing best preserved");
    for(auto const&r:records){p.inputs+=str(r,"kind")==((integer(p.metadata,"nativeInputVersion")>=2)?"dispatch":"input");if(integer(p.metadata,"nativeInputVersion")>=2&&str(r,"kind")=="frame"&&r["preInputs"].isArray())p.inputs+=r["preInputs"].size();p.poses+=str(r,"kind")=="pose";p.ticks=std::max(p.ticks,integer(r,"tick"));p.elapsed=std::max(p.elapsed,number(r,"elapsed"));}
    if(p.bytes+encoded.size()>MaxFileBytes-16384)throw std::runtime_error("Replay storage budget exceeded; best preserved");
    diskBudget(encoded.size()+16384);
    {std::ofstream out(pending(attempt),std::ios::binary|std::ios::app);out<<encoded;
      if(final){Json f;f["footer"]=true;f["payloadSha256"]=str(b,"sha256");f["recordCount"]=static_cast<std::int64_t>(p.count+records.size());f["captureCompleteness"]=records.back()["capturePartial"].asBool().unwrapOr(false)?"PARTIAL_RECOVERED":"COMPLETE";f["replayValidationStatus"]="AWAITING_OFFICIAL_RECEIPT";
      f["terminalReason"]=records.back()["reason"];f["bestProgress"]=records.back()["progress"];f["cleared"]=number(records.back(),"progress")==100;
      f["endedAt"]=static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
      f["durationMs"]=p.elapsed*1000;f["nativeTickCount"]=p.ticks;f["inputEventCount"]=static_cast<std::int64_t>(p.inputs);f["poseSampleCount"]=static_cast<std::int64_t>(p.poses);
      f["payloadBytes"]=static_cast<std::int64_t>(p.bytes+encoded.size());f["footerSha256"]=sha256Text(f.dump(matjson::NO_INDENTATION));out<<f.dump(matjson::NO_INDENTATION)<<'\n';}
      out.flush();if(!out)throw std::runtime_error("Replay block write failed");}durable(pending(attempt));
    p.chain=str(b,"sha256");p.bytes+=encoded.size();p.count+=records.size();++p.sequence;
    if(final)m_pending.erase(found);
    },charge);
}
std::future<void> Storage::discard(std::string attempt){return submit([this,attempt]{
    auto m=manifest();m["pending"].erase(sha256Text(attempt));saveManifest(m);fs::remove(pending(attempt));m_pending.erase(attempt);
});}
std::future<bool> Storage::reconcile(Receipt r){return submit([this,r]{
    auto path=pending(r.attemptId);if(!fs::exists(path))return false;auto d=decode(path,true);
    if(!d.complete){
      if(!r.accepted||!r.ended||d.records.empty()||str(d.records.front(),"kind")!="initial")return false;
      auto pose=std::find_if(d.records.rbegin(),d.records.rend(),[](auto const& item){return str(item,"kind")=="pose";});
      if(pose==d.records.rend()||score(number(*pose,"progress"),str(d.metadata,"phase")=="TIEBREAK")!=score(r.progress,str(d.metadata,"phase")=="TIEBREAK"))return false;
      fs::resize_file(path,d.validBytes);Json terminal;terminal["kind"]="terminal";terminal["tick"]=d.records.back()["tick"];terminal["elapsed"]=d.records.back()["elapsed"];
      terminal["progress"]=(*pose)["progress"];terminal["reason"]="PROCESS_RECOVERY";auto b=block({terminal},d.chain,0);
      std::size_t sequences=0;{std::ifstream scan(path);std::string line;while(std::getline(scan,line))++sequences;}b["sequence"]=static_cast<std::int64_t>(sequences-1);
      bool const hasTerminal=str(d.records.back(),"kind")=="terminal";
      if(hasTerminal && score(number(d.records.back(),"progress"),str(d.metadata,"phase")=="TIEBREAK")!=score(r.progress,str(d.metadata,"phase")=="TIEBREAK"))return false;
      Json footer;footer["footer"]=true;footer["payloadSha256"]=hasTerminal?Json(d.chain):b["sha256"];footer["recordCount"]=static_cast<std::int64_t>(d.records.size()+(hasTerminal?0:1));
      footer["captureCompleteness"]="PARTIAL_RECOVERED";footer["replayValidationStatus"]="AWAITING_OFFICIAL_RECEIPT";footer["terminalReason"]="PROCESS_RECOVERY";footer["bestProgress"]=hasTerminal?d.records.back()["progress"]:(*pose)["progress"];
      footer["payloadBytes"]=static_cast<std::int64_t>(d.validBytes+(hasTerminal?0:b.dump(matjson::NO_INDENTATION).size()+1));
      footer["footerSha256"]=sha256Text(footer.dump(matjson::NO_INDENTATION));
      {std::ofstream out(path,std::ios::app);if(!hasTerminal)out<<b.dump(matjson::NO_INDENTATION)<<'\n';out<<footer.dump(matjson::NO_INDENTATION)<<'\n';out.flush();if(!out)throw std::runtime_error("Replay recovery seal failed");}
      durable(path);d=decode(path);
    }
    if(!receiptMatches(d,r))return false;auto m=manifest();auto key=roundStorageKey(d.metadata);auto old=str(m["best"][key],"file");
    bool tie=str(d.metadata,"phase")=="TIEBREAK";
    if((!r.bestAttemptId.empty()&&r.bestAttemptId!=r.attemptId)||score(r.progress,tie)<score(r.roundBest,tie)){
      m["pending"].erase(sha256Text(r.attemptId));saveManifest(m);fs::remove(path);return false;
    }
    if(!old.empty()){
      if(old!="survival/"+key+"/"+sha256Text(str(m["best"][key],"attemptId"))+".replay")throw std::runtime_error("Replay manifest path invalid");
      auto prior=decode(m_root/old);
      if(!(r.cleared&&!m["best"][key]["cleared"].asBool().unwrapOr(false))&&!improves(r.progress,number(m["best"][key],"progress"),tie)){
        m["pending"].erase(sha256Text(r.attemptId));saveManifest(m);fs::remove(path);return false;
      }
    }
    // Do not rewrite the hash-chained header. Authority is a manifest receipt.
    auto file="survival/"+key+"/"+sha256Text(r.attemptId)+".replay";auto dest=m_root/file;safeDirectories(dest.parent_path());
    if(fs::exists(dest))throw std::runtime_error("Replay immutable file already exists");replace(path,dest);
    Json entry;entry["file"]=file;entry["progress"]=r.progress;entry["cleared"]=r.cleared;entry["attemptId"]=r.attemptId;entry["payloadSha256"]=d.chain;
    entry["verifiedFrameCount"]=static_cast<std::int64_t>(verifiedReplayFrames(d,r));entry["officialBestAttemptId"]=r.bestAttemptId;
    auto allFrames=static_cast<std::size_t>(std::count_if(d.records.begin(),d.records.end(),[](auto const& v){return str(v,"kind")=="frame";}));
    entry["officialCompletion"]=verifiedReplayFrames(d,r)==allFrames&&score(number(d.records.back(),"progress"),tie)==score(r.progress,tie)&&((number(d.records.back(),"progress")==100)==r.cleared)?"FULL_CAPTURE":"OFFICIAL_PREFIX";
    m["best"][key]=entry;m["pending"].erase(sha256Text(r.attemptId));
    try{saveManifest(m);}catch(...){replace(dest,path);throw;}
    if(!old.empty()&&old!=file)fs::remove(m_root/old);return true;
});}
std::optional<Document> Storage::loadBest(Json const& identity){
    auto m=manifest();auto key=roundStorageKey(identity);auto file=str(m["best"][key],"file");if(file.empty())return std::nullopt;
    auto expected="survival/"+key+"/"+sha256Text(str(m["best"][key],"attemptId"))+".replay";if(file!=expected)throw std::runtime_error("Replay manifest path invalid");
    auto d=decode(m_root/file);if(roundStorageKey(d.metadata)!=key||str(m["best"][key],"payloadSha256")!=d.chain)throw std::runtime_error("Replay best integrity failed");
    d.metadata["officialBestProgress"]=m["best"][key]["progress"];d.metadata["verifiedFrameCount"]=m["best"][key]["verifiedFrameCount"];d.metadata["officialCompletion"]=m["best"][key]["officialCompletion"];return d;
}
std::future<std::optional<Document>> Storage::best(Json identity){return submit([this,identity]{return loadBest(identity);});}
std::future<std::unique_ptr<Playback>> Storage::playback(Json identity){return submit([this,identity]()->std::unique_ptr<Playback>{auto d=loadBest(identity);return d?std::make_unique<Playback>(std::move(*d)):nullptr;});}
std::future<std::optional<Json>> Storage::inspect(Json identity){return submit([this,identity]()->std::optional<Json>{auto d=loadBest(identity);return d?std::optional(d->metadata):std::nullopt;});}
std::future<void> Storage::journalClose(Json metadata){return submit([this,metadata]{
    auto const& e=metadata["closingEvent"];
    if(!metadataValid(metadata)||str(e,"type")!="ATTEMPT_END"||str(e,"attemptId")!=str(metadata,"attemptId")||str(e,"roundKey")!=str(metadata,"roundKey")||integer(e,"mapGeneration")!=integer(metadata,"mapGeneration")||integer(e,"levelId")!=integer(metadata,"levelId")||str(e,"eventId").empty()||score(number(e,"progress"),str(metadata,"phase")=="TIEBREAK")<0)throw std::runtime_error("Replay closing ownership invalid");
    auto m=manifest();auto key=sha256Text(str(metadata,"attemptId"));
    if(!m["closing"].isObject())m["closing"]=Json::object();
    bool exists=m["closing"].contains(key);
    if(exists&&m["closing"][key]["closingEvent"]!=e)throw std::runtime_error("Replay closing event changed");
    if(m["closing"].size()>=MaxPending&&!exists)throw std::runtime_error("Replay closing journal capacity reached");
    m["closing"][key]=metadata;saveManifest(m);
});}
std::future<void> Storage::closeAcknowledged(std::string attempt){return submit([this,attempt]{auto m=manifest();if(m["closing"].erase(sha256Text(attempt)))saveManifest(m);});}
std::future<std::vector<Json>> Storage::recover(){return submit([this]{
    safeDirectories(m_root/"pending");std::vector<Json> result;auto m=manifest();
    // Recover the journal window between immutable rename and manifest commit.
    // An active best is never moved or guessed from an orphan's percentage.
    if(m["pending"].isObject())for(auto const& metadata:m["pending"]){
        if(!metadataValid(metadata))continue;
        auto source=pending(str(metadata,"attemptId"));auto round=roundStorageKey(metadata);
        auto relative="survival/"+round+"/"+sha256Text(str(metadata,"attemptId"))+".replay";
        auto orphan=m_root/relative;
        if(!fs::exists(source)&&fs::exists(orphan)&&str(m["best"][round],"file")!=relative){
            try{auto d=decode(orphan);if(d.metadata==metadata)replace(orphan,source);}catch(...){/* Preserve both known best and damaged orphan. */}
        }
    }
    for(auto const& entry:fs::directory_iterator(m_root/"pending")){
      if(result.size()>=MaxPending)break;if(!entry.is_regular_file()||entry.is_symlink()||entry.path().extension()!=".tmp")continue;
      try{auto d=decode(entry.path(),true);if(entry.path()!=pending(str(d.metadata,"attemptId")))continue;
        if(!d.complete){fs::resize_file(entry.path(),d.validBytes);d.metadata["captureCompleteness"]="PARTIAL_RECOVERED";}
        auto const& cm=m;if(str(cm["closing"][sha256Text(str(d.metadata,"attemptId"))]["closingEvent"],"type")=="ATTEMPT_END")d.metadata["closingEvent"]=cm["closing"][sha256Text(str(d.metadata,"attemptId"))]["closingEvent"];
        result.push_back(d.metadata);
      }catch(...){/* A corrupt pending file never authorizes deleting an existing best. */}
    }
    if(m["closing"].isObject())for(auto const& metadata:m["closing"]){if(result.size()>=MaxPending)break;if(metadataValid(metadata)&&std::none_of(result.begin(),result.end(),[&](auto const& e){return str(e,"attemptId")==str(metadata,"attemptId");}))result.push_back(metadata);}
    return result;
});}
}
