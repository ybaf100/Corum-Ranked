#include "SurvivalThumbnailProvider.hpp"
#include "domain/SurvivalThumbnailPolicy.hpp"
#include <fstream>
#include <iterator>
#include <set>
using namespace geode::prelude;
namespace corum::ranked {
namespace {
SurvivalThumbnailProvider* liveProvider=nullptr;std::uint64_t appEpoch=0;
std::optional<std::string> thumbnailHeader(web::WebResponse const& response,std::string_view wanted){
    // Geode preserves the wire spelling and its header() lookup is case
    // sensitive. HTTP/2 sends lower-case names; HTTP field names are not.
    for(auto const& name:response.headers()){
        auto lower=[](unsigned char c){return c>='A' && c<='Z'?c+('a'-'A'):c;};
        if(name.size()!=wanted.size() || !std::equal(name.begin(),name.end(),wanted.begin(),[&](auto a,auto b){return lower(a)==lower(b);}))continue;
        if(auto value=response.header(name))return std::string(*value);
    }
    return {};
}
}
std::uint64_t SurvivalThumbnailProvider::applicationEpoch(){return appEpoch;}
void SurvivalThumbnailProvider::onApplicationPause(){
    ++appEpoch;if(liveProvider){liveProvider->deactivate();liveProvider->m_invalidTextureCache=true;}
}
std::int64_t SurvivalThumbnailProvider::now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string SurvivalThumbnailProvider::key(Job const& job){return std::to_string(job.id)+(job.medium?"-medium":"-small");}
bool SurvivalThumbnailProvider::supported(){
    auto* loader=Loader::get();
    // ImagePlus declares the public CCImage format extension used by the
    // official thumbnail client. No internal manager, symbol or ABI is called.
    return loader && loader->isModLoaded("cdc.level_thumbnails") && loader->isModLoaded("prevter.imageplus");
}
SurvivalThumbnailProvider& SurvivalThumbnailProvider::get(){
    static auto* owner=[](){auto* p=new SurvivalThumbnailProvider;p->init();liveProvider=p;
        p->m_directory=Mod::get()->getSaveDir()/"survival-thumbnails-v1";
        std::error_code ec;std::filesystem::create_directories(p->m_directory,ec);p->trimDisk();
        CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(p,0,false);return p;}();
    return *owner;
}
void SurvivalThumbnailProvider::activate(SurvivalSelectionContext const& context){
    // App suspension may already have released the platform GL context. Drop
    // its textures only when a visible UI frame next activates this provider.
    if(m_invalidTextureCache){m_cache.clear();m_invalidTextureCache=false;}
    auto const enabled=supported();auto const identity=context.key()+":"+std::to_string(context.sceneGeneration);
    if(identity==m_context && enabled==m_enabled)return;
    deactivate();m_context=identity;m_selected=context.levelId;m_enabled=enabled;m_randomRequests=0;m_httpRequests=0;
    m_liveness=std::make_shared<std::atomic_bool>(enabled);
    if(!enabled)return;
    if(m_selected){enqueue({m_selected,false});enqueue({m_selected,true},true);}
}
void SurvivalThumbnailProvider::deactivate(){
    if(m_liveness)m_liveness->store(false);
    ++m_generation;for(auto& slot:m_slots){++slot.ticket;slot.request.cancel();slot.busy=false;}
    m_jobs.clear();m_encoded.clear();m_context.clear();m_enabled=false;m_selected=0;
    // An in-flight CPU decode is reaped on the main thread, never joined here.
}
void SurvivalThumbnailProvider::release(SurvivalSelectionContext const& context){
    if(m_context==context.key()+":"+std::to_string(context.sceneGeneration))deactivate();
}
void SurvivalThumbnailProvider::enqueue(Job job,bool front){
    if(!m_enabled)return;
    auto const k=key(job);if(!job.random && (m_cache.contains(k) || m_negative[k]>now()))return;
    auto same=[&](Job const& other){return other.id==job.id && other.medium==job.medium && other.random==job.random;};
    if(!job.random && (std::any_of(m_jobs.begin(),m_jobs.end(),same) || std::any_of(m_slots.begin(),m_slots.end(),[&](auto const& s){return s.busy && same(s.job);}) ||
        std::any_of(m_encoded.begin(),m_encoded.end(),[&](auto const& e){return same(e.job);}) || (m_decode && same(m_decode->job))))return;
    if(front)m_jobs.push_front(job);else m_jobs.push_back(job);
}
void SurvivalThumbnailProvider::begin(std::size_t index,Job job){
    if(job.random && m_randomRequests>=14)return;
    if(m_httpRequests>=24)return;
    ++m_httpRequests;
    if(job.random)++m_randomRequests;
    auto& slot=m_slots[index];slot.busy=true;slot.job=job;slot.began=now();auto const ticket=++slot.ticket,generation=m_generation;
    slot.exceeded=std::make_shared<std::atomic_bool>(false);auto exceeded=slot.exceeded;
    auto request=web::WebRequest().userAgent("CorumRanked-SurvivalThumbnails/1").timeout(std::chrono::seconds(4)).followRedirects(false).certVerification(true);
    request.onProgress([exceeded](web::WebProgress const& p){if(p.downloaded()>SurvivalThumbnailMaxBytes || p.downloadTotal()>SurvivalThumbnailMaxBytes)exceeded->store(true);});
    auto const url=job.random?std::string(SurvivalThumbnailOrigin)+"/thumbnail/random/small":survivalThumbnailUrl(job.id,job.medium);
    slot.request.spawn(request.get(url),[this,index,generation,ticket](web::WebResponse response){this->response(index,generation,ticket,std::move(response));});
}
void SurvivalThumbnailProvider::failed(Job job,int status){
    if(job.random)return;
    if(status!=404 && job.attempt==0){job.attempt=1;job.notBefore=now()+1000;enqueue(job);return;}
    m_negative[key(job)]=now()+(status==404?300000:10000);
    while(m_negative.size()>128)m_negative.erase(m_negative.begin());
}
void SurvivalThumbnailProvider::response(std::size_t index,std::uint64_t generation,std::uint64_t ticket,web::WebResponse response){
    auto& slot=m_slots[index];if(generation!=m_generation || ticket!=slot.ticket || !slot.busy || !m_enabled)return;
    auto job=slot.job;slot.busy=false;
    auto const code=response.code();
    if(job.random && code==302){
        auto location=thumbnailHeader(response,"Location");auto id=location?survivalThumbnailRedirect(std::string_view(*location)):std::nullopt;
        if(id && !m_cache.contains(key({*id,false})))enqueue({*id,false},false);
        return;
    }
    if(code!=200 || slot.exceeded->load() || response.data().size()>SurvivalThumbnailMaxBytes){failed(job,code);return;}
    auto type=thumbnailHeader(response,"Content-Type");
    if(!type || !std::string_view(*type).starts_with("image/webp") || !survivalWebpSize(response.data())){failed(job,422);return;}
    [[maybe_unused]] auto const received=response.data().size();
    auto bytes=std::move(response).data();
    if(m_encoded.size()<3)m_encoded.push_back({job,generation,std::move(bytes)});else failed(job,429);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    log::info("[SurvivalThumbnail] network id={} medium={} bytes={} elapsedMs={}",job.id,job.medium,received,now()-slot.began);
#endif
}
void SurvivalThumbnailProvider::decodeNext(){
    if(m_decode || m_encoded.empty() || !m_enabled)return;
    auto encoded=std::move(m_encoded.front());m_encoded.pop_front();
    if(encoded.generation!=m_generation)return;
    auto job=encoded.job;auto bytes=std::move(encoded.bytes);
    auto path=m_directory/(key(job)+".webp");auto active=m_liveness;auto generation=m_generation;
    try{m_decode.emplace(Decode{job,encoded.generation,std::async(std::launch::async,[bytes=std::move(bytes),path,active,generation,job]()mutable{
        Decoded decoded;auto const began=now();
        if(!job.network){
            std::error_code ec;
            if(!std::filesystem::is_regular_file(path,ec) || std::filesystem::file_size(path,ec)>SurvivalThumbnailMaxBytes || ec)return decoded;
            std::ifstream input(path,std::ios::binary);bytes.resize(SurvivalThumbnailMaxBytes+1);
            input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));bytes.resize(static_cast<std::size_t>(input.gcount()));
        }
        auto size=survivalWebpSize(bytes);if(!size)return decoded;
        auto image=std::make_unique<CCImage>();
        if(image->initWithImageData(bytes.data(),static_cast<int>(bytes.size())) &&
            image->getWidth()==size->first && image->getHeight()==size->second){
            decoded.image=std::move(image);
            if(job.network && active && active->load()){
                auto temporary=path;temporary+=".part-"+std::to_string(generation);
                {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<char const*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
                std::error_code ec;
                if(active->load())std::filesystem::rename(temporary,path,ec);
                std::filesystem::remove(temporary,ec);
            }
        }
        decoded.millis=now()-began;return decoded;
    })});}catch(std::exception const&){failed(job,422);}
}
void SurvivalThumbnailProvider::cache(Decode const& work,Decoded decoded){
    if(!m_enabled || work.generation!=m_generation)return;
    if(!decoded.image){
        std::error_code ec;std::filesystem::remove(m_directory/(key(work.job)+".webp"),ec);
        auto job=work.job;if(!job.network){job.network=true;enqueue(job,true);}else failed(job,422);return;
    }
    [[maybe_unused]] auto const uploadBegan=now();
    auto texture=Ref<CCTexture2D>(new CCTexture2D);texture->release();
    if(!texture->initWithImage(decoded.image.get())){failed(work.job,422);return;}
    auto const k=key(work.job);
    m_cache[k]={texture,static_cast<std::size_t>(decoded.image->getWidth())*decoded.image->getHeight()*4,now(),work.job.id,work.job.medium};
    m_negative.erase(k);trim();trimDisk();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    log::info("[SurvivalThumbnail] decoded id={} medium={} source={} decodeMs={} textureUploadMs={} texture={}x{}",work.job.id,work.job.medium,work.job.network?"network":"disk",decoded.millis,now()-uploadBegan,decoded.image->getWidth(),decoded.image->getHeight());
#endif
}
void SurvivalThumbnailProvider::trim(){
    auto total=[&]{std::size_t n=0;for(auto const& [k,e]:m_cache)n+=e.bytes;return n;};
    while(m_cache.size()>12 || total()>16*1024*1024){
        auto victim=m_cache.end();for(auto it=m_cache.begin();it!=m_cache.end();++it)
            if(it->second.id!=m_selected && (victim==m_cache.end() || it->second.used<victim->second.used))victim=it;
        if(victim==m_cache.end())break;m_cache.erase(victim);
    }
}
void SurvivalThumbnailProvider::trimDisk(){
    std::error_code ec;std::vector<std::filesystem::directory_entry> files;std::uintmax_t total=0;
    for(auto const& entry:std::filesystem::directory_iterator(m_directory,ec))if(entry.is_regular_file(ec)){
        if(entry.path().extension()!=".webp"){std::filesystem::remove(entry.path(),ec);continue;}
        auto size=entry.file_size(ec);if(size>SurvivalThumbnailMaxBytes){std::filesystem::remove(entry.path(),ec);continue;}
        files.push_back(entry);total+=size;
    }
    std::sort(files.begin(),files.end(),[&](auto const& a,auto const& b){return a.last_write_time(ec)<b.last_write_time(ec);});
    auto remaining=files.size();
    for(auto const& file:files){if(remaining<=32 && total<=32*1024*1024)break;
        auto size=file.file_size(ec);if(std::filesystem::remove(file.path(),ec)){total-=std::min(total,size);--remaining;}
    }
}
void SurvivalThumbnailProvider::update(float){
    if(m_decode && m_decode->result.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        // Retire the completed ticket before enqueuing a cache-miss/network
        // retry; otherwise duplicate suppression sees its own finished decode.
        auto work=std::move(*m_decode);m_decode.reset();
        try{auto decoded=work.result.get();cache(work,std::move(decoded));}
        catch(std::exception const&){if(m_enabled && work.generation==m_generation)failed(work.job,422);}
    }
    if(!m_enabled)return;
    if(!supported()){deactivate();return;}
    for(auto& slot:m_slots)if(slot.busy && (slot.exceeded->load() || now()-slot.began>5500)){
        auto job=slot.job;++slot.ticket;slot.request.cancel();slot.busy=false;failed(job,408);
    }
    decodeNext();
    auto const smallThumbnailCount=std::count_if(m_cache.begin(),m_cache.end(),[](auto const& e){return !e.second.medium;});
    auto const outstanding=std::count_if(m_jobs.begin(),m_jobs.end(),[](auto const& j){return !j.medium;})+
        std::count_if(m_slots.begin(),m_slots.end(),[](auto const& slot){return slot.busy&&!slot.job.medium;})+
        static_cast<int>(m_encoded.size())+static_cast<int>(m_decode.has_value());
    if(smallThumbnailCount+outstanding<7 && m_randomRequests<14 && m_httpRequests<24 && m_jobs.size()<3)enqueue({0,false,true});
    auto selectedDisk=[&](Job const& job){return !job.random && !job.network && job.id==m_selected;};
    auto const awaitingSelectedDisk=std::any_of(m_jobs.begin(),m_jobs.end(),selectedDisk) ||
        std::any_of(m_encoded.begin(),m_encoded.end(),[&](auto const& e){return selectedDisk(e.job);}) ||
        (m_decode && selectedDisk(m_decode->job));
    for(std::size_t i=0;i<m_slots.size();++i){
        if(m_slots[i].busy || m_jobs.empty())continue;
        auto it=std::find_if(m_jobs.begin(),m_jobs.end(),[&](auto const& job){return job.notBefore<=now() && (!job.random || !awaitingSelectedDisk);});if(it==m_jobs.end())continue;
        auto job=*it;m_jobs.erase(it);
        if(!job.random && !job.network){
            if(m_encoded.size()>=3){m_jobs.push_front(job);break;}
            m_encoded.push_back({job,m_generation,{}});continue;
        }
        begin(i,job);
    }
}
Ref<CCTexture2D> SurvivalThumbnailProvider::selected(){
    if(m_invalidTextureCache)return {};
    for(bool medium:{true,false}){auto it=m_cache.find(key({m_selected,medium}));if(it!=m_cache.end()){it->second.used=now();return it->second.texture;}}
    return {};
}
std::vector<std::pair<std::uint64_t,Ref<CCTexture2D>>> SurvivalThumbnailProvider::previews(){
    std::vector<std::pair<std::uint64_t,Ref<CCTexture2D>>> result;std::set<std::uint64_t> ids;
    if(m_invalidTextureCache)return result;
    for(auto& [k,e]:m_cache)if(!e.medium && ids.insert(e.id).second){e.used=now();result.emplace_back(e.id,e.texture);}
    return result;
}
}
