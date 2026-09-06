"""Pack a curated set of (n)ASAP performances for the GitHub Pages demo (docs/pieces/).

For each piece: the performance MIDI, the MusicXML score (gzip) and the nASAP note
alignment (gzip), plus docs/pieces.json with composer / title / nickname keywords for the
text search.  Performances and scores are CC BY-NC-SA 4.0 (ASAP / nASAP); see docs/README.md."""
import csv,gzip,json,re,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
ASAP=Path('/Users/john/sources/segno/tools/synth-ab/corpora/asap')
OUT=ROOT/'docs/pieces'
# (metadata title, display title, keywords); the performance is taken from app/pieces.json when present
CATALOG=[
 ('Bach','Fugue_bwv_846','Fugue in C major, BWV 846 (WTC I)','well-tempered clavier polyphony'),
 ('Bach','Prelude_bwv_846','Prelude in C major, BWV 846 (WTC I)','well-tempered clavier'),
 ('Bach','Fugue_bwv_883','Fugue in F-sharp minor, BWV 883 (WTC II)','well-tempered clavier polyphony'),
 ('Bach','Italian_concerto','Italian Concerto, BWV 971','' ),
 ('Balakirev','Islamey','Islamey, oriental fantasy','virtuoso'),
 ('Beethoven','Piano_Sonatas_1-1','Piano Sonata No. 1 in F minor, Op. 2 No. 1: I','opus 2'),
 ('Beethoven','Piano_Sonatas_8-1','Piano Sonata No. 8 in C minor, Op. 13 "Pathétique": I','pathetique'),
 ('Beethoven','Piano_Sonatas_8-2','Piano Sonata No. 8 in C minor, Op. 13 "Pathétique": II Adagio cantabile','pathetique slow'),
 ('Beethoven','Piano_Sonatas_26-1','Piano Sonata No. 26 in E-flat major, Op. 81a "Les Adieux": I','lebewohl farewell'),
 ('Beethoven','Piano_Sonatas_30-1','Piano Sonata No. 30 in E major, Op. 109: I','opus 109'),
 ('Beethoven','Piano_Sonatas_14-3','Piano Sonata No. 14 in C-sharp minor, Op. 27 No. 2 "Moonlight": III','moonlight presto'),
 ('Beethoven','Piano_Sonatas_17-3','Piano Sonata No. 17 in D minor, Op. 31 No. 2 "Tempest": III','tempest'),
 ('Beethoven','Piano_Sonatas_21-1','Piano Sonata No. 21 in C major, Op. 53 "Waldstein": I','waldstein'),
 ('Beethoven','Piano_Sonatas_23-1','Piano Sonata No. 23 in F minor, Op. 57 "Appassionata": I','appassionata'),
 ('Beethoven','Piano_Sonatas_31-1','Piano Sonata No. 31 in A-flat major, Op. 110: I','opus 110'),
 ('Brahms','Six_Pieces_op_118_2','Intermezzo in A major, Op. 118 No. 2','klavierstücke'),
 ('Chopin','Ballades_1','Ballade No. 1 in G minor, Op. 23',''),
 ('Chopin','Barcarolle','Barcarolle in F-sharp major, Op. 60',''),
 ('Chopin','Berceuse_op_57','Berceuse in D-flat major, Op. 57','lullaby'),
 ('Chopin','Etudes_op_10_3','Étude Op. 10 No. 3 in E major "Tristesse"','tristesse'),
 ('Chopin','Etudes_op_10_5','Étude Op. 10 No. 5 in G-flat major "Black Keys"','black keys'),
 ('Chopin','Etudes_op_10_12','Étude Op. 10 No. 12 in C minor "Revolutionary"','revolutionary'),
 ('Chopin','Etudes_op_25_1','Étude Op. 25 No. 1 in A-flat major "Aeolian Harp"','aeolian harp'),
 ('Chopin','Etudes_op_25_2','Étude Op. 25 No. 2 in F minor',''),
 ('Chopin','Etudes_op_25_11','Étude Op. 25 No. 11 in A minor "Winter Wind"','winter wind'),
 ('Chopin','Scherzos_31','Scherzo No. 2 in B-flat minor, Op. 31',''),
 ('Chopin','Sonata_2_3rd','Piano Sonata No. 2 in B-flat minor, Op. 35: III Marche funèbre','funeral march'),
 ('Debussy','Images_Book_1_1_Reflets_dans_lEau','Images, Book I: Reflets dans l\'eau','reflections in the water impressionist'),
 ('Debussy','Pour_le_Piano_1','Pour le piano: Prélude',''),
 ('Glinka','The_Lark','The Lark (arr. Balakirev)','zhavoronok'),
 ('Haydn','Keyboard_Sonatas_50-1','Keyboard Sonata in C major, Hob. XVI:50: I',''),
 ('Liszt','Annees_de_pelerinage_2_1_Gondoliera','Années de pèlerinage: Venezia e Napoli, Gondoliera',''),
 ('Liszt','Concert_Etude_S145_1','Concert Étude S. 145 No. 1 "Waldesrauschen"','forest murmurs'),
 ('Liszt','Gran_Etudes_de_Paganini_2_La_campanella','Grandes études de Paganini No. 3 "La campanella"','campanella bell'),
 ('Liszt','Transcendental_Etudes_10','Transcendental Étude No. 10 in F minor','allegro agitato molto'),
 ('Mozart','Piano_Sonatas_11-3','Piano Sonata No. 11 in A major, K. 331: III Rondo alla turca','turkish march'),
 ('Mozart','Piano_Sonatas_12-1','Piano Sonata No. 12 in F major, K. 332: I',''),
 ('Mozart','Fantasie_475','Fantasia in C minor, K. 475',''),
 ('Prokofiev','Toccata','Toccata in D minor, Op. 11','repeated notes'),
 ('Rachmaninoff','Preludes_op_23_4','Prelude in D major, Op. 23 No. 4',''),
 ('Rachmaninoff','Preludes_op_32_5','Prelude in G major, Op. 32 No. 5',''),
 ('Rachmaninoff','Preludes_op_32_10','Prelude in B minor, Op. 32 No. 10',''),
 ('Ravel','Gaspard_de_la_Nuit_1_Ondine','Gaspard de la nuit: Ondine',''),
 ('Ravel','Pavane','Pavane pour une infante défunte',''),
 ('Ravel','Miroirs_3_Une_Barque','Miroirs: Une barque sur l\'océan',''),
 ('Schubert','Impromptu_op.90_D.899_2','Impromptu in E-flat major, Op. 90 No. 2, D. 899',''),
 ('Schubert','Impromptu_op.90_D.899_3','Impromptu in G-flat major, Op. 90 No. 3, D. 899',''),
 ('Schubert','Impromptu_op.90_D.899_4','Impromptu in A-flat major, Op. 90 No. 4, D. 899',''),
 ('Schubert','Impromptu_op142_3','Impromptu in B-flat major, Op. 142 No. 3, D. 935 (Theme and variations)','rosamunde'),
 ('Schumann','Arabeske','Arabeske in C major, Op. 18',''),
 ('Schumann','Kreisleriana_1','Kreisleriana, Op. 16: I Äußerst bewegt',''),
 ('Scriabin','Etudes_op_8_11','Étude Op. 8 No. 11 in B-flat minor',''),
 ('Scriabin','Sonatas_5','Piano Sonata No. 5, Op. 53',''),
]
def slug(s): return re.sub(r'[^a-z0-9]+','-',s.lower()).strip('-')
def main():
    rows=list(csv.DictReader(open(ASAP/'metadata.csv')))
    app=json.load(open(ROOT/'app/pieces.json'))
    chosen={}
    for p in app:
        if '/corpora/asap/' in p['path']: chosen[p['path'].split('/asap/')[1].rsplit('/',1)[0]]=p
    OUT.mkdir(parents=True,exist_ok=True); pieces=[]; total=0
    for composer,mtitle,title,kw in CATALOG:
        cands=[r for r in rows if r['composer']==composer and r['title'] in (mtitle,mtitle+'_no_repeat') and (ASAP/r['midi_performance']).with_name(Path(r['midi_performance']).stem+'_note_alignments').exists()]
        if not cands: print('MISSING',composer,mtitle); continue
        folder=cands[0]['folder']; extra=chosen.get(folder)
        if extra: pick=[r for r in cands if r['midi_performance'].endswith(Path(extra['path']).name)][0]
        else:
            good=[r for r in cands if r['robust_note_alignment']=='1.0'] or cands
            pick=sorted(good,key=lambda r:r['midi_performance'])[len(good)//2]
        perf=ASAP/pick['midi_performance']; score=ASAP/pick['xml_score']; align=perf.parent/(perf.stem+'_note_alignments')/'note_alignment.tsv'   # the CSV path is mangled for titles with a dot
        pid=slug(composer+'-'+mtitle); d=OUT/pid; d.mkdir(exist_ok=True)
        shutil.copyfile(perf,d/'perf.mid')
        for src,dst in [(score,'score.musicxml.gz'),(align,'align.tsv.gz')]:
            with open(src,'rb') as f, gzip.open(d/dst,'wb',9) as g: shutil.copyfileobj(f,g)
        # duration from the alignment's last onset
        last=0.0
        for line in gzip.open(d/'align.tsv.gz','rt'):
            c=line.rstrip('\n').split('\t')
            if len(c)>=6 and c[0]!='xml_id':
                try: last=max(last,float(c[5]))
                except ValueError: pass
        size=sum(f.stat().st_size for f in d.iterdir()); total+=size
        pieces.append(dict(id=pid,composer=composer,title=title,performer=Path(pick['midi_performance']).stem,
            source=pick['midi_performance'],duration=round(last+3,1),tags=extra['tags'] if extra else [],
            why=extra.get('why','') if extra else '',keywords=kw,bytes=size))
        print(f'{pid:50s} {size/1024:7.0f} KB  {pick["midi_performance"]}')
    json.dump(pieces,open(ROOT/'docs/pieces.json','w'),indent=1,ensure_ascii=False)
    print(len(pieces),'pieces,',round(total/1e6,1),'MB')
if __name__=='__main__': main()
