rm -f page_models/page_models_list.txt

for d in ~/mychrome/38.0.2125.122/model_extractor/samples/*; do
    pagename=`basename $d`
    ~/mychrome/38.0.2125.122/model_extractor/main.py \
        change-hostnames \
        ${d}/page_model-10252016.json.bz2 page_models/${pagename}-page_model.json \
        server{1,2,3,4,5,6,7,8,9,10}
    echo "${pagename} page_models/${pagename}-page_model.json" >> page_models/page_models_list.txt
done
